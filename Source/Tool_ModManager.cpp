// Modified by Cheddarbung - November 2026. Based on Hades Workshop 0.53 by Tirlititi. Distributed under GNU GPL v3.

#include "Tool_ModManager.h"
#include "WorkspaceDiff.h"

//#include <wx/tokenzr.h>
//#include <algorithm>
//#include "main.h"
#include "Gui_TextureEditor.h"
//#include "Gui_LoadingDialog.h"
#include "Hades_Strings.h"
//#include "Steam_Strings.h"
#include "Database_SpellAnimation.h"

#include "Gui_ScriptEditor.h"
#include "Database_Script.h"
#include "main.h"
#include <algorithm>
#include <memory>
#include <array>
#include <map>
#include <set>
#include <wx/checklst.h>
#include <wx/file.h>
#include <wx/filepicker.h>
#include <wx/fileconf.h>
#include <wx/dir.h>
#include <wx/stdpaths.h>
#include <wx/msw/wrapwin.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")

bool IsAutoAppendModeActivated(int scripttype);

namespace {
constexpr size_t WorkspaceMaxBytes = 4 * 1024 * 1024;

wxString WorkspaceNormalize(wxString text) {
	text.Replace("\r\n", "\n");
	text.Replace("\r", "\n");
	return text;
}

wxString WorkspaceHash(const wxString& text) {
	const auto bytes = text.utf8_str();
	BCRYPT_ALG_HANDLE algorithm = nullptr;
	if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
		return {};
	UCHAR digest[32];
	const auto status = BCryptHash(algorithm, nullptr, 0,
		reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())), static_cast<ULONG>(bytes.length()), digest, sizeof(digest));
	BCryptCloseAlgorithmProvider(algorithm, 0);
	wxString hex;
	if (status >= 0)
		for (const auto part : digest)
			hex += wxString::Format("%02x", static_cast<unsigned int>(part));
	return hex;
}

bool WorkspaceRead(const wxString& path, wxString& text) {
	wxLogNull quiet;
	wxFile file(path);
	const auto length = file.IsOpened() ? file.Length() : -1;
	if (length < 0 || length > WorkspaceMaxBytes)
		return false;
	std::string bytes(static_cast<size_t>(length), '\0');
	if (length && file.Read(bytes.data(), bytes.size()) != length)
		return false;
	if (file.Length() != length || bytes.find('\0') != std::string::npos)
		return false;
	if (bytes.compare(0, 3, "\xef\xbb\xbf") == 0)
		bytes.erase(0, 3);
	text = wxString::FromUTF8(bytes.data(), bytes.size());
	if (!bytes.empty() && text.empty())
		return false;
	text = WorkspaceNormalize(text);
	return true;
}

bool WorkspaceWrite(const wxString& path, const wxString& text) {
	wxTempFile file(path);
	return file.IsOpened() && file.Write(text, wxConvUTF8) && file.Commit();
}

wxString ProjectWorkspaceRoot() {
	const wxFileName exe(wxStandardPaths::Get().GetExecutablePath());
	return exe.GetPathWithSep() + "CodexWorkspace";
}

wxString ProjectWorkspaceFolder(const CDDataStruct& project) {
	const wxString root = ProjectWorkspaceRoot();
	wxFileName path(project.projectFilename);
	if (root.empty() || project.projectFilename.empty() || project.gametype != GAME_TYPE_STEAM ||
		!path.Normalize(wxPATH_NORM_ABSOLUTE | wxPATH_NORM_DOTS))
		return {};
	const auto key = WorkspaceHash(path.GetFullPath().Lower() + "\n" + wxString(project.filename).Lower());
	if (key.empty())
		return {};
	wxString slug;
	for (const auto ch : path.GetName().Lower()) {
		if (slug.size() == 35) break;
		slug += (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ? ch : wxUniChar('_');
	}
	return root + "/project_" + slug + "_" + key
		+ wxString::Format("/lang_%u_%u", static_cast<unsigned int>(GetSteamLanguage()), hades::STEAM_SINGLE_LANGUAGE_MODE ? 1u : 0u);
}

bool MakeWorkspaceDirectory(const wxString& path) {
	return wxDirExists(path) || wxFileName::Mkdir(path, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
}

struct WorkspaceTarget {
	ScriptDataStruct* script;
	EnemyDataStruct* enemies;
	TextDataStruct* text;
	int type, id;
	unsigned int index;
	bool modified;
	SteamLanguage language = STEAM_LANGUAGE_NONE;

	bool IsText() const { return language != STEAM_LANGUAGE_NONE; }

	wxString Name() const {
		if (IsText())
			return wxString("TextBlocks/") + HADES_STRING_STEAM_LANGUAGE_SHORT_NAME[language] + wxString::Format("/%d.txt", id);
		return (type == SCRIPT_TYPE_FIELD ? "Fields/" : "Battles/") + wxString::Format("%d.txt", id);
	}
};

std::vector<WorkspaceTarget> WorkspaceTargets(CDDataStruct& project) {
	std::vector<WorkspaceTarget> targets;
	if (project.saveset.sectionloaded[DATA_SECTION_FIELD]) {
		auto& data = project.fieldset;
		for (unsigned int i = 0; i < data.amount; ++i) {
			auto* script = data.script_data[i];
			targets.push_back({script, nullptr, data.related_text[i], SCRIPT_TYPE_FIELD, data.GetIdByIndex(i), i,
				data.addition[i] || script->modified || script->IsDataModified() || (script->parent_cluster && script->parent_cluster->modified)});
		}
	}
	if (project.saveset.sectionloaded[DATA_SECTION_ENMY]) {
		auto& data = project.enemyset;
		for (unsigned int i = 0; i < data.battle_amount; ++i) {
			auto* script = data.script[i];
			targets.push_back({script, data.battle[i], data.text[i], SCRIPT_TYPE_BATTLE, data.GetIdByIndex(i), i,
				script->modified || script->IsDataModified() || data.battle[i]->modified || data.battle_data[i]->modified ||
				data.text[i]->modified || (script->parent_cluster && script->parent_cluster->modified)});
		}
	}
	for (unsigned int i = 0; i < project.textset.amount && project.saveset.sectionloaded[DATA_SECTION_TEXT]; ++i) {
		auto* block = project.textset.text_data[i];
		if (!block)
			continue;
		for (SteamLanguage lang = 0; lang < STEAM_LANGUAGE_AMOUNT; ++lang)
			if (!hades::STEAM_SINGLE_LANGUAGE_MODE || lang == GetSteamLanguage())
				targets.push_back({nullptr, nullptr, block, 0, project.textset.struct_id[i], i, true, lang});
	}
	return targets;
}

struct WorkspaceUnit {
	unsigned int entry, function;
	wxString marker, text;
};

std::vector<WorkspaceUnit> WorkspaceUnits(ScriptEditHandler& handler) {
	handler.GenerateFunctionList();
	handler.GenerateEntryNames();
	handler.GenerateFunctionStrings(true);
	std::vector<WorkspaceUnit> units;
	for (unsigned int e = 0; e < handler.script.entry_amount; ++e) {
		const auto& entry = handler.script.entry[e];
		for (unsigned int f = 0; f < entry.function_amount; ++f) {
			const auto type = entry.func[f].function_type;
			wxString text = WorkspaceNormalize(handler.func_str[e][f]);
			if (!text.EndsWith("\n"))
				text += "\n";
			units.push_back({e, f, wxString::Format("#HW unit %u %u %u\n", e, f, static_cast<unsigned int>(type)), text});
		}
	}
	return units;
}

wxString WorkspaceHeader(const WorkspaceTarget& target) {
	return wxString::Format("#HW workspace 1 %s %d\n", target.type == SCRIPT_TYPE_FIELD ? "FIELD" : "BATTLE", target.id);
}

wxString WorkspaceDump(const WorkspaceTarget& target, const std::vector<WorkspaceUnit>& units) {
	wxString result = WorkspaceHeader(target);
	for (const auto& unit : units)
		result += unit.marker + unit.text;
	return result;
}

bool WorkspaceFileUnits(const wxString& input, const WorkspaceTarget& target,
	const std::vector<WorkspaceUnit>& units, std::vector<wxString>& text) {
	const wxString header = WorkspaceHeader(target);
	if (!input.StartsWith(header))
		return false;
	size_t position = header.length();
	for (const auto& unit : units) {
		if (input.Mid(position, unit.marker.length()) != unit.marker)
			return false;
		position += unit.marker.length();
		const size_t next = input.find("\n#HW ", position);
		const size_t end = next == wxString::npos ? input.length() : next + 1;
		wxString body = input.Mid(position, end - position);
		if (body.BeforeFirst('\n') != unit.text.BeforeFirst('\n'))
			return false;
		for (const auto& line : wxSplit(body.AfterFirst('\n'), '\n'))
			if (wxString(line).Trim(false).StartsWith("Function "))
				return false;
		text.push_back(body);
		position = end;
	}
	return position == input.length();
}

wxString WorkspaceTextHeader(const WorkspaceTarget& target) {
	return wxString("#HW filetype TEXT\n\n#HW language ") + HADES_STRING_STEAM_LANGUAGE_SHORT_NAME[target.language]
		+ wxString::Format("\n#HW fileid %d\n", target.id);
}

wxString WorkspaceTextDump(const WorkspaceTarget& target, const TextDataStruct& block) {
	auto result = WorkspaceTextHeader(target);
	for (unsigned int i = 0; i < block.text.size(); ++i) {
		const auto& entry = block.text[i];
		if (entry.txt.multi_lang_init[target.language])
			result += wxString::Format("#HW text %d\n", i < block.base_amount ? static_cast<int>(i) : entry.id)
				+ WorkspaceNormalize(wxString(entry.txt.multi_lang_str[target.language])) + "\n\n";
	}
	return result;
}

bool HasDialoguePrefix(const wxString& value) {
	if (!value.StartsWith("[STRT="))
		return false;
	const auto end = value.find(']', 6);
	if (end == wxString::npos)
		return false;
	const auto size = value.Mid(6, end - 6);
	unsigned long width, lines;
	return size.Contains(",") && size.BeforeFirst(',').ToULong(&width) && size.AfterFirst(',').ToULong(&lines);
}

LogStruct StageWorkspaceText(const WorkspaceTarget& target, const wxString& external, TextDataStruct& block) {
	LogStruct result;
	const auto header = WorkspaceTextHeader(target);
	if (!external.StartsWith(header)) {
		result.AddError(L"Keep the #HW filetype, language, and fileid header intact.\n");
		return result;
	}
	const wxString marker = "#HW text ";
	std::set<int> seen;
	int nextId = static_cast<int>(block.base_amount);
	for (size_t i = block.base_amount; i < block.text.size(); ++i)
		nextId = std::max(nextId, block.text[i].id + 1);
	for (size_t pos = header.size(); pos < external.size();) {
		const auto lineEnd = external.find('\n', pos);
		unsigned long id;
		if (lineEnd == wxString::npos || external.Mid(pos, marker.size()) != marker ||
			!external.Mid(pos + marker.size(), lineEnd - pos - marker.size()).ToULong(&id) ||
			id >= UINT16_MAX || !seen.insert(static_cast<int>(id)).second) {
			result.AddError(L"Expected a unique #HW text ID from 0 through 65534.\n");
			return result;
		}
		const auto start = lineEnd + 1;
		const auto next = external.find("\n#HW ", start);
		const auto end = next == wxString::npos ? external.size() : next + 1;
		auto value = external.Mid(start, end - start);
		// HWS batch text separates entries with two newlines.
		if (value.EndsWith("\n")) value.RemoveLast();
		if (value.EndsWith("\n")) value.RemoveLast();
		pos = end;
		const int index = block.GetTextIndexById(static_cast<int>(id));
		const auto* existing = index < 0 ? nullptr : &block.text[index].txt;
		const bool initialized = existing && existing->multi_lang_init[target.language];
		const auto old = initialized ? WorkspaceNormalize(wxString(existing->multi_lang_str[target.language])) : wxString();
		if (initialized && value == old)
			continue;
		if ((!value.empty() && (!initialized || old.StartsWith("[STRT=")) && !HasDialoguePrefix(value)) ||
			value.utf8_str().length() > UINT16_MAX - 6) {
			result.AddError(wxString::Format("Text %lu needs a valid [STRT=width,lines] prefix or is too long.\n", id).ToStdWstring());
			return result;
		}
		if (existing) {
			if (block.SetText(index, value.ToStdWstring(), target.language) == 0)
				continue;
		} else if (id == static_cast<unsigned long>(nextId)) {
			FF9String entry;
			entry.SetValue(value.ToStdWstring(), target.language);
			if (block.AddText(nextId, entry) == 0) {
				++nextId;
				continue;
			}
		}
		result.AddError(wxString::Format("Could not add text %lu. Append new entries starting at ID %d.\n", id, nextId).ToStdWstring());
		return result;
	}
	for (unsigned int i = 0; i < block.text.size(); ++i) {
		const int id = i < block.base_amount ? static_cast<int>(i) : block.text[i].id;
		if (block.text[i].txt.multi_lang_init[target.language] && !seen.count(id)) {
			result.AddError(wxString::Format("Text %d is missing. Keep existing IDs, including empty entries.\n", id).ToStdWstring());
			break;
		}
	}
	return result;
}

wxString WorkspacePath(const wxString& folder, const WorkspaceTarget& target) {
	return folder + "/" + target.Name();
}

wxString BaselinePath(const wxString& folder, const WorkspaceTarget& target) {
	return folder + "/.hws-baseline/" + target.Name();
}

bool WriteWorkspaceBaseline(const wxString& folder, const WorkspaceTarget& target,
	const wxString& canonical, const wxString& accepted) {
	const wxString baseline = BaselinePath(folder, target);
	const auto hash = WorkspaceHash(accepted);
	return !hash.empty() && WorkspaceWrite(baseline, canonical) && WorkspaceWrite(baseline + ".sha256", hash);
}

wxString ProjectOpcodeReference() {
	wxString reference = "# Hades Workshop opcode reference\n\n"
		"Generated from HWS metadata. Zeros show argument positions only. Keep command names and argument counts exact.\n\n";
	for (const auto& item : HADES_STRING_SCRIPT_OPCODE) {
		const auto& op = item.second;
		reference += "## " + wxString(op.label) + "\n\n";
		if (op.id < 7 || op.id == 0x0B || op.id == 0xFF || op.arg_amount < 0 || wxString(op.label).StartsWith("0x"))
			reference += "Internal, variable, or undocumented signature. Use the existing syntax documented below.\n\n";
		else {
			wxString call = wxString(op.label) + "(";
			for (int i = 0; i < op.arg_amount; ++i)
				call += i == 0 ? "0" : ", 0";
			reference += call + ")\n\n";
		}
		if (!op.help.empty())
			reference += wxString(op.help) + "\n\n";
	}
	reference += "## Expression functions\n\n";
	for (const auto& item : FlexibleFunctionNameList)
		reference += "- " + wxString(item.second.name) + wxString::Format(" (%u arguments)\n", static_cast<unsigned int>(item.second.argcount));
	return reference;
}

bool PrepareWorkspace(const wxString& folder) {
	for (const auto& relative : {"Fields", "Battles", "TextBlocks", ".hws-baseline/Fields", ".hws-baseline/Battles"})
		if (!MakeWorkspaceDirectory(folder + "/" + relative))
			return false;
	if (!wxFileExists(folder + "/opcodes.md") && !WorkspaceWrite(folder + "/opcodes.md", ProjectOpcodeReference()))
		return false;
	if (!MakeWorkspaceDirectory(folder + "/Metadata") || !WorkspaceWrite(folder + "/Metadata/AGENTS.md",
		"# Generated HWS variable metadata\n\n"
		"Read Fields/<id>.txt or Battles/<id>.txt here before scripting to check each entry's allocation and declarations.\n"
		"These are generated snapshots of HWS in-memory data, refreshed on Parse and Save, even with pending external edits.\n"
		"Do not edit these files. They are not imported. Ask the user to change allocations in HWS.\n"
		"A snapshot does not prove the current game export contains these allocations.\n"))
		return false;
	const auto textGuide = folder + "/TextBlocks/AGENTS.md";
	if (!wxFileExists(textGuide) && !WorkspaceWrite(textGuide,
		"# Hades Workshop text blocks\n\n"
		"Edit only the requested block and language. Preserve the #HW header and every existing ID.\n"
		"Append a new entry with #HW text followed by the next local ID on its own line.\n"
		"Start its dialogue with a native [STRT=width,lines] tag, for example [STRT=40,1]Hello.\n"
		"Keep any existing HWS formatting tags and leave a blank line between entries.\n"
		"Apply dialogue and any script edits that reference it together in Review Changes.\n"))
		return false;
	if (wxFileExists(folder + "/AGENTS.md")) {
		wxString guide;
		if (!WorkspaceRead(folder + "/AGENTS.md", guide))
			return false;
		if (guide.Contains("Metadata/"))
			return true;
		return WorkspaceWrite(folder + "/AGENTS.md", guide +
			"\nConsult Metadata/Fields/<id>.txt or Metadata/Battles/<id>.txt for generated in-memory variable allocations and declarations. Do not edit metadata; ask the user to change allocations in HWS.\n");
	}
	return WorkspaceWrite(folder + "/AGENTS.md",
		"# Hades Workshop project workspace\n\n"
		"Each file in Fields/ and Battles/ contains the existing functions of one field or battle.\n"
		"Keep the #HW markers and function declarations intact. Edit statements inside existing functions only.\n"
		"TextBlocks/ contains one file per block and loaded language. Keep the header and existing #HW text IDs.\n"
		"Append new entries with the next ID and a native [STRT=width,lines] prefix, for example [STRT=40,1]Hello.\n"
		"Consult opcodes.md. Use existing HWS syntax. Leave .hws files and .hws-baseline alone.\n"
		"Consult Metadata/Fields/<id>.txt or Metadata/Battles/<id>.txt for generated in-memory allocations and declarations. Do not edit metadata.\n"
		"Hades Workshop reviews and parses the edits before applying them to the open project.\n");
}

// Informational only: never imported or included in pending-script comparisons.
wxString WorkspaceVariableDeclarations(const ScriptLocalVariableSet& vars, const wxString& scope) {
	wxString result;
	for (unsigned int i = 0; i < vars.amount; ++i) {
		result += scope + " ";
		if (vars.type[i] == SCRIPT_VARIABLE_TYPE_BOOL)
			result += "bool ";
		else {
			result += vars.type[i] == SCRIPT_VARIABLE_TYPE_INT ? "int" : "uint";
			result += wxString::Format("%u ", static_cast<unsigned int>(vars.size[i]));
		}
		result += wxString(vars.name[i]) + " " + wxString(VarOpList[vars.cat[i]].opstring)
			+ wxString::Format("%u\n", static_cast<unsigned int>(vars.id[i]));
	}
	return result;
}

bool WriteWorkspaceMetadata(CDDataStruct& project, const wxString& folder, const WorkspaceTarget& target) {
	if (target.IsText())
		return true;
	const auto& script = *target.script;
	if (script.local_data.size() < script.entry_amount)
		return false;
	ScriptEditHandler handler(*target.script, target.type, &project.saveset, target.enemies, target.text);
	handler.GenerateFunctionList();
	handler.GenerateEntryNames();
	wxString content = "# HWS variable metadata v1\n"
		"# Generated from the current in-memory HWS script; not proof of a saved/exported game build.\n"
		"# Read-only reference. Edit allocations/declarations in HWS, then Parse or Save to refresh.\n"
		"# Pending external script edits are not reflected here until applied.\n";
	content += "# Script: " + target.Name() + "\n\n";
	content += "# Global declarations\n" + WorkspaceVariableDeclarations(script.global_data, "global");
	for (unsigned int i = 0; i < script.entry_amount; ++i) {
		content += wxString::Format("\n# Entry %u: ", i) + handler.entry_name[i] + "\n";
		content += wxString::Format("allocate %u\n", static_cast<unsigned int>(script.local_data[i].allocate_amount));
		content += WorkspaceVariableDeclarations(script.local_data[i], "local");
	}
	const auto path = folder + "/Metadata/" + target.Name();
	if (!MakeWorkspaceDirectory(wxFileName(path).GetPath()))
		return false;
	wxString previous;
	return (WorkspaceRead(path, previous) && previous == content) || WorkspaceWrite(path, content);
}
enum class WorkspaceSyncResult { Ready, Pending, Failed };

WorkspaceSyncResult SyncWorkspaceTarget(CDDataStruct& project, const wxString& folder, const WorkspaceTarget& target) {
	if (!WriteWorkspaceMetadata(project, folder, target))
		return WorkspaceSyncResult::Failed;
	const auto path = WorkspacePath(folder, target);
	const auto baseline = BaselinePath(folder, target);
	if (target.IsText() && (!MakeWorkspaceDirectory(wxFileName(path).GetPath()) ||
		!MakeWorkspaceDirectory(wxFileName(baseline).GetPath())))
		return WorkspaceSyncResult::Failed;
	wxString external, accepted, previous;
	if (wxFileExists(path)) {
		const bool tracked = WorkspaceRead(path, external) && WorkspaceRead(baseline, previous)
			&& WorkspaceRead(baseline + ".sha256", accepted);
		if (!tracked || accepted.empty() || WorkspaceHash(external) != accepted)
			return WorkspaceSyncResult::Pending;
	}
	wxString content;
	if (target.IsText())
		content = WorkspaceTextDump(target, *target.text);
	else {
		ScriptEditHandler handler(*target.script, target.type, &project.saveset, target.enemies, target.text);
		content = WorkspaceDump(target, WorkspaceUnits(handler));
	}
	if (content.utf8_str().length() > WorkspaceMaxBytes)
		return WorkspaceSyncResult::Failed;
	if (content == previous && !accepted.empty())
		return WorkspaceSyncResult::Ready;
	wxString latest;
	if ((!accepted.empty() && (!WorkspaceRead(path, latest) || latest != external)) ||
		(accepted.empty() && wxFileExists(path)))
		return WorkspaceSyncResult::Pending;
	if (!WriteWorkspaceBaseline(folder, target, content, content) || !WorkspaceWrite(path, content))
		return WorkspaceSyncResult::Failed;
	return WorkspaceSyncResult::Ready;
}

struct WorkspaceChange {
	WorkspaceTarget target;
	wxString baseline, external, acceptedHash, canonical;
	std::unique_ptr<ScriptDataStruct> staged;
};

using StagedTexts = std::map<TextDataStruct*, TextDataStruct>;

LogStruct StageWorkspaceTextChange(WorkspaceChange& change, StagedTexts& texts) {
	const auto& target = change.target;
	if (WorkspaceTextDump(target, *target.text) != change.baseline) {
		LogStruct log;
		log.AddError(L"This text block changed in HWS since it was synchronized. Reconcile both versions before applying.\n");
		return log;
	}
	auto& staged = texts.try_emplace(target.text, *target.text).first->second;
	return StageWorkspaceText(target, change.external, staged);
}

std::set<int64_t> WorkspaceTextReferences(const ScriptFunction& function, SteamLanguage language) {
	std::set<int64_t> references;
	for (int index : function.indices[language]) {
		if (index < 0 || static_cast<size_t>(index) >= function.op.size())
			continue;
		const auto& op = function.op[index];
		const auto metadata = HADES_STRING_SCRIPT_OPCODE.find(op.opcode);
		if (metadata == HADES_STRING_SCRIPT_OPCODE.end() || metadata->second.arg_amount <= 0)
			continue;
		for (int i = 0; i < metadata->second.arg_amount && i < op.arg_amount; ++i)
			if (metadata->second.arg_type[i] == AT_TEXT && !op.arg[i].is_var)
				references.insert(op.arg[i].GetValue());
	}
	return references;
}

LogStruct StageWorkspaceChange(SaveSet& data, WorkspaceChange& change, StagedTexts& texts) {
	auto& target = change.target;
	ScriptEditHandler handler(*target.script, target.type, &data, target.enemies, target.text);
	const auto units = WorkspaceUnits(handler);
	LogStruct result;
	std::vector<wxString> proposals;
	if (WorkspaceDump(target, units) != change.baseline) {
		result.AddError(L"The HWS script changed since it was synchronized. Keep both versions and reconcile the edits before applying.\n");
		return result;
	}
	if (!WorkspaceFileUnits(change.external, target, units, proposals)) {
		result.AddError(L"Workspace identity, function markers or declarations changed. Keep the original file structure.\n");
		return result;
	}
	const auto stagedText = texts.find(target.text);
	if (stagedText != texts.end())
		handler.text = &stagedText->second;
	// Entry sizes can be stale after HWS merges or batch imports. Refresh the
	// staged copy for this language before unsigned function-size subtraction.
	handler.script.GetDataSize(GetSteamLanguage());
	int64_t extra = handler.script.GetExtraSize();
	for (size_t index = 0; index < units.size(); ++index) {
		const auto& unit = units[index];
		if (proposals[index] == unit.text)
			continue;
		handler.currentvar_str = handler.localvar_str[unit.entry];
		auto log = handler.ParseFunction(proposals[index], unit.entry, unit.function);
		if (log.ok) {
			const auto budget = extra + static_cast<int64_t>(handler.script.entry[unit.entry].size) - handler.GetParsedEntryNewSize();
			if (budget < 0)
				log.AddError(L"Not enough space for this function.\n");
			else {
				extra = budget;
				const auto language = GetSteamLanguage();
				auto& function = handler.script.entry[unit.entry].func[unit.function];
				const auto oldReferences = target.type == SCRIPT_TYPE_FIELD && handler.text
					? WorkspaceTextReferences(function, language) : std::set<int64_t>();
				handler.ApplyParsedFunction();
				if (target.type == SCRIPT_TYPE_FIELD && handler.text) {
					for (const auto id : WorkspaceTextReferences(handler.script.entry[unit.entry].func[unit.function], language)) {
						if (oldReferences.count(id))
							continue;
						const int index = id >= 0 && id < UINT16_MAX ? handler.text->GetTextIndexById(static_cast<int>(id)) : -1;
						if (index < 0 || !handler.text->text[index].txt.multi_lang_init[language])
							log.AddError(wxString::Format("Dialogue ID %lld is missing from the field's text block. Include the matching text file in this review.\n",
								static_cast<long long>(id)).ToStdWstring());
					}
				}
				if (IsAutoAppendModeActivated(target.type) || handler.script.append_mode == 1) {
					handler.script.append_mode = 1;
					handler.script.entry[unit.entry].append_mode |= 1;
				}
			}
		}
		if (log.error_amount)
			result.AddError((unit.text.BeforeFirst('\n') + "\n").ToStdWstring());
		result.AddLog(log);
	}
	if (result.ok) {
		// GenerateFunctionStrings appends to the handler's existing function text.
		ScriptEditHandler stagedHandler(handler.script, target.type, &data, target.enemies, handler.text);
		change.canonical = WorkspaceDump(target, WorkspaceUnits(stagedHandler));
		change.staged = std::make_unique<ScriptDataStruct>();
		*change.staged = handler.script;
	}
	return result;
}


// Compare by edit distance so distant small changes do not become one huge replacement.
void WorkspaceDiff(wxTextCtrl& output, const wxString& before, const wxString& after) {
    const auto left = wxSplit(before, '\n');
    const auto right = wxSplit(after, '\n');
    const auto diff = hws_workspace::CompareLines(left, right);
    const wxColour neutral(221, 227, 235), removed(255, 153, 157), added(152, 218, 165);
    if (diff.simplified) {
        output.SetDefaultStyle(wxTextAttr(wxColour(255, 205, 128)));
        output.AppendText(_("Comparison limit reached: the changed region below is shown as a whole-block replacement, not a precise line diff.\n\n"));
    }
    for (const auto& line : diff.lines) {
        const bool isAdded = line.kind == hws_workspace::DiffKind::Add;
        const bool isRemoved = line.kind == hws_workspace::DiffKind::Remove;
        output.SetDefaultStyle(wxTextAttr(isAdded ? added : isRemoved ? removed : neutral));
        output.AppendText(wxString(isAdded ? "+ " : isRemoved ? "- " : "  ") +
            (isAdded ? right[line.index] : left[line.index]) + "\n");
    }
    output.SetInsertionPoint(0);
}
}


void ToolModManager::CreateWorkspaceRoot() {
	MakeWorkspaceDirectory(ProjectWorkspaceRoot());
}

void ToolModManager::SyncProjectWorkspace(CDDataStruct& project) {
	const auto folder = ProjectWorkspaceFolder(project);
	if (folder.empty())
		return;
	project.InitText();
	if (!PrepareWorkspace(folder)) {
		wxLogWarning(_("Could not prepare the project script workspace."));
		return;
	}
	int pending = 0, errors = 0;
	for (const auto& target : WorkspaceTargets(project)) {
		if (!target.modified && !wxFileExists(BaselinePath(folder, target)))
			continue;
		switch (SyncWorkspaceTarget(project, folder, target)) {
		case WorkspaceSyncResult::Pending: ++pending; break;
		case WorkspaceSyncResult::Failed: ++errors; break;
		default: break;
		}
	}
	if (pending || errors)
		wxLogWarning(_("Project workspace: %d file(s) with pending edits kept, %d write error(s)."), pending, errors);
}

bool ToolModManager::SyncTextBlockWorkspace(CDDataStruct& project, unsigned int index, wxWindow* parent) {
	const auto folder = ProjectWorkspaceFolder(project);
	if (folder.empty() || !PrepareWorkspace(folder))
		return false;
	bool found = false, ready = true;
	for (const auto& target : WorkspaceTargets(project)) {
		if (!target.IsText() || target.index != index)
			continue;
		found = true;
		const auto result = SyncWorkspaceTarget(project, folder, target);
		if (result == WorkspaceSyncResult::Failed) {
			wxLogWarning(_("Could not write %s to Codex Workspace."), target.Name());
			ready = false;
		} else if (result == WorkspaceSyncResult::Pending) {
			ready = false;
			if (parent)
				wxMessageBox(_("Pending text edits were preserved. Use Review Changes to apply them."),
					_("Project Workspace"), wxOK | wxICON_INFORMATION, parent);
		}
	}
	return found && ready;
}
bool ToolModManager::SyncFieldWorkspace(CDDataStruct& project, int fieldId, ScriptDataStruct& script, wxWindow* parent) {
	const auto folder = ProjectWorkspaceFolder(project);
	if (folder.empty()) {
		if (parent)
			wxMessageBox(_("Open a saved Steam .hws project first."),
				_("Project Workspace"), wxOK | wxICON_INFORMATION, parent);
		return false;
	}
	const int index = project.fieldset.GetIndexById(fieldId);
	if (index < 0 || !PrepareWorkspace(folder)) {
		wxLogError(_("Could not prepare this field's workspace."));
		return false;
	}
	WorkspaceTarget target{&script, nullptr, project.fieldset.related_text[index], SCRIPT_TYPE_FIELD, fieldId,
		static_cast<unsigned int>(index), true};
	const auto result = SyncWorkspaceTarget(project, folder, target);
	if (result == WorkspaceSyncResult::Pending && parent)
		wxMessageBox(_("This file already has external edits. Review them in Modifications Manager."),
			_("Project Workspace"), wxOK | wxICON_INFORMATION, parent);
	else if (result == WorkspaceSyncResult::Failed)
		wxLogError(_("Could not write this field to the project workspace."));
	if (target.text) {
		const int textIndex = project.textset.GetIndexById(target.text->block_id);
		if (textIndex >= 0)
			SyncTextBlockWorkspace(project, textIndex);
	}
	return result == WorkspaceSyncResult::Ready;
}

bool ToolModManager::HasPendingWorkspaceChanges(const CDDataStruct& project) {
    // Inspect only files: UI updates must never initialize or export project data.
    const auto folder = ProjectWorkspaceFolder(project);
    const auto baselineRoot = folder + "/.hws-baseline/";
    if (folder.empty() || !wxDirExists(baselineRoot))
        return false;
    // Cache content comparisons, but invalidate on precise file timestamps or sizes.
    // This keeps idle UI refreshes from repeatedly hashing every script in the project.
    using Stamp = std::array<ULONGLONG, 6>;
    static wxString cachedFolder;
    static std::map<wxString, std::pair<Stamp, bool>> cache;
    if (cachedFolder != folder) {
        cachedFolder = folder;
        cache.clear();
    }
    const auto stampFile = [](const wxString& path, Stamp& stamp, size_t offset) {
        WIN32_FILE_ATTRIBUTE_DATA info;
        if (!GetFileAttributesExW(path.wc_str(), GetFileExInfoStandard, &info) ||
            (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            return false;
        stamp[offset] = (static_cast<ULONGLONG>(info.ftLastWriteTime.dwHighDateTime) << 32) | info.ftLastWriteTime.dwLowDateTime;
        stamp[offset + 1] = (static_cast<ULONGLONG>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
        return true;
    };
    wxArrayString hashes;
    wxDir::GetAllFiles(baselineRoot, &hashes, "*.sha256", wxDIR_FILES | wxDIR_DIRS);
    for (const auto& path : hashes) {
        wxFileName relative(path);
        if (!relative.MakeRelativeTo(baselineRoot))
            continue;
        auto name = relative.GetFullPath();
        name.RemoveLast(7); // Strip .sha256, leaving the workspace filename.
        Stamp stamp{};
        if (!stampFile(path, stamp, 0) || !stampFile(folder + "/" + name, stamp, 2) ||
            !stampFile(baselineRoot + name, stamp, 4)) {
            cache.erase(path);
            continue;
        }
        const auto cached = cache.find(path);
        if (cached != cache.end() && cached->second.first == stamp) {
            if (cached->second.second)
                return true;
            continue;
        }
        wxString accepted, external;
        if (WorkspaceRead(path, accepted) && !accepted.empty() && WorkspaceRead(folder + "/" + name, external)) {
            const bool pending = WorkspaceHash(external) != accepted;
            cache[path] = {stamp, pending};
            if (pending)
                return true;
        } else {
            cache.erase(path);
        }
    }
    return false;
}

void ToolModManager::ReviewWorkspace(CDDataStruct& project, wxWindow* parent) {
	auto* cddata = &project;
	const auto folder = ProjectWorkspaceFolder(*cddata);
	if (!wxDirExists(folder)) {
		wxMessageBox(_("Open a saved Steam .hws project to export its scripts and text blocks to Codex Workspace."), _("Project Workspace"), wxOK | wxICON_INFORMATION, parent);
		return;
	}
	std::vector<WorkspaceChange> changes;
	wxArrayString names;
	int unreadable = 0;
	for (const auto& target : WorkspaceTargets(*cddata)) {
		const auto baseline = BaselinePath(folder, target);
		if (!wxFileExists(baseline))
			continue;
		WorkspaceChange change{target};
		if (!WorkspaceRead(baseline, change.baseline) || !WorkspaceRead(baseline + ".sha256", change.acceptedHash)
			|| !WorkspaceRead(WorkspacePath(folder, target), change.external)) {
			++unreadable;
			continue;
		}
		if (WorkspaceHash(change.external) != change.acceptedHash) {
			names.Add(target.Name());
			changes.push_back(std::move(change));
		}
	}
	if (unreadable)
		wxMessageBox(wxString::Format(_("%d tracked file(s) are missing or unreadable. They are excluded from parent review."), unreadable),
			_("Project Workspace"), wxOK | wxICON_INFORMATION, parent);

	wxDialog dialog(parent, wxID_ANY, _("Review Workspace Changes"), wxDefaultPosition,
		parent->FromDIP(wxSize(1000, 650)), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
	auto* layout = new wxBoxSizer(wxVERTICAL);
	layout->Add(new wxStaticText(&dialog, wxID_ANY, _("Check the files to apply. All selected changes must validate.")),
		0, wxALL, 8);
	auto* body = new wxBoxSizer(wxHORIZONTAL);
	auto* list = new wxCheckListBox(&dialog, wxID_ANY, wxDefaultPosition, parent->FromDIP(wxSize(250, -1)), names);
	auto* diff = new wxTextCtrl(&dialog, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
		wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2 | wxTE_DONTWRAP);
	diff->SetBackgroundColour(wxColour(35, 38, 43));
	const auto showDiff = [&]() {
		const int row = list->GetSelection();
		if (row == wxNOT_FOUND)
			return;
		wxTextCtrl& output = *diff;
		output.Freeze();
		output.Clear();
		WorkspaceDiff(output, changes[row].baseline, changes[row].external);
		output.Thaw();
	};
	list->Bind(wxEVT_LISTBOX, [&](wxCommandEvent&) { showDiff(); });
	for (unsigned int i = 0; i < names.size(); ++i)
		list->Check(i);
	if (!changes.empty()) { list->SetSelection(0); showDiff(); }
    else diff->SetValue("No external changes. Export is available for the current open project.");
	body->Add(list, 0, wxEXPAND | wxRIGHT, 6);
	body->Add(diff, 1, wxEXPAND);
	layout->Add(body, 1, wxEXPAND | wxLEFT | wxRIGHT, 8);

    const wxString settingsPath = wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath() + "/HwsSession.ini";
    wxFileConfig settings(wxEmptyString, wxEmptyString, settingsPath, wxEmptyString, wxCONFIG_USE_LOCAL_FILE);
    wxString exportPath = GetTopWindow()->SteamSaveDir.GetPath();
    settings.Read("WorkspaceReviewExportFolder", &exportPath, exportPath);
    auto* exportRow = new wxBoxSizer(wxHORIZONTAL);
    exportRow->Add(new wxStaticText(&dialog, wxID_ANY, "Export folder"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    auto* destination = new wxDirPickerCtrl(&dialog, wxID_ANY, exportPath, "Choose destination mod folder", wxDefaultPosition, wxDefaultSize, wxDIRP_DEFAULT_STYLE | wxDIRP_USE_TEXTCTRL);
    exportRow->Add(destination, 1, wxEXPAND);
    layout->Add(exportRow, 0, wxEXPAND | wxALL, 8);
    layout->Add(new wxStaticText(&dialog, wxID_ANY, "Parse applies checked changes. Export writes the current project; field visual assets are preserved."), 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    buttons->Add(new wxButton(&dialog, wxID_CANCEL, "Cancel"), 0, wxALL, 4);
    auto* apply = new wxButton(&dialog, wxID_OK, "Parse");
    auto* exportOnly = new wxButton(&dialog, wxID_SAVE, "Export");
    auto* parseExport = new wxButton(&dialog, wxID_APPLY, "Parse and Export");
    buttons->Add(apply, 0, wxALL, 4);
    buttons->Add(exportOnly, 0, wxALL, 4);
    buttons->Add(parseExport, 0, wxALL, 4);
    apply->Enable(!changes.empty());
    parseExport->Enable(!changes.empty() && project.config.dll_usage != 0);
    exportOnly->Enable(project.config.dll_usage != 0);
    layout->Add(buttons, 0, wxALIGN_RIGHT | wxALL, 4);
    const auto requestExport = [&](wxCommandEvent& event) {
        const wxString path = destination->GetPath();
        if (path.empty() || !wxFileName(path, wxEmptyString).IsAbsolute()) {
            wxMessageBox("Choose an absolute destination mod folder before exporting.", "Export", wxOK | wxICON_INFORMATION, &dialog);
            return;
        }
        settings.Write("WorkspaceReviewExportFolder", path);
        settings.Flush();
        dialog.EndModal(event.GetId());
    };
    exportOnly->Bind(wxEVT_BUTTON, requestExport);
    parseExport->Bind(wxEVT_BUTTON, requestExport);
    dialog.SetSizer(layout);
    dialog.CentreOnParent();
    const int action = dialog.ShowModal();
    if (action != wxID_OK && action != wxID_SAVE && action != wxID_APPLY) return;
    const auto exportProject = [&]() {
        bool sections[DATA_SECTION_AMOUNT];
        for (int i = 0; i < DATA_SECTION_AMOUNT; ++i) sections[i] = project.saveset.sectionmodified[i];
        // Use the existing Memoria/raw-asset exporter. Never delete unrelated destination files.
        const int result = CreateSteamMod(destination->GetPath().ToStdString(), sections, project.config, project.saveset, 1, 1, false);
        if (result == 0 || result == -1) {
            GetTopWindow()->SteamSaveDir.AssignDir(destination->GetPath());
            SyncProjectWorkspace(project);
            wxMessageBox(result == 0
                ? "Export completed. Field visual assets were preserved. Save the HWS project separately to retain your edits."
                : "Export completed with the existing C# compatibility warning. Save the HWS project separately.",
                "Export", wxOK | (result == 0 ? wxICON_INFORMATION : wxICON_WARNING), parent);
        } else {
            wxMessageBox(wxString::Format("Export failed (code %d). Applied edits remain in the open project; save it and retry Export. The destination may contain a partial export.", result),
                "Export", wxOK | wxICON_ERROR, parent);
        }
    };
    if (action == wxID_SAVE) { exportProject(); return; }

	std::vector<WorkspaceChange*> selected;
	StagedTexts texts;
	LogStruct diagnostics;
	const auto stage = [&](bool textFiles) {
		for (unsigned int i = 0; i < changes.size(); ++i) {
			if (!list->IsChecked(i))
				continue;
			auto& change = changes[i];
			if (change.target.IsText() != textFiles)
				continue;
			wxString latest, baseline, hash;
			const auto path = BaselinePath(folder, change.target);
			LogStruct log;
			if (!WorkspaceRead(WorkspacePath(folder, change.target), latest) || latest != change.external ||
				!WorkspaceRead(path, baseline) || baseline != change.baseline ||
				!WorkspaceRead(path + ".sha256", hash) || hash != change.acceptedHash)
				log.AddError(L"Workspace changed during review. Review again.\n");
			else
				log = textFiles ? StageWorkspaceTextChange(change, texts)
					: StageWorkspaceChange(cddata->saveset, change, texts);
			if (log.error_amount)
				diagnostics.AddError((change.target.Name() + "\n").ToStdWstring());
			if (log.warning_amount)
				diagnostics.AddWarning((change.target.Name() + "\n").ToStdWstring());
			diagnostics.AddLog(log);
			selected.push_back(&change);
		}
	};
	stage(true);
	if (diagnostics.ok)
		stage(false);
	if (selected.empty())
		return;
	if (!diagnostics.ok) {
		LogDialog errors(parent, diagnostics);
		errors.ShowModal();
		return;
	}
	// Recheck every selected external file after validation and before changing any script.
	for (const auto* change : selected) {
		wxString latest;
		if (!WorkspaceRead(WorkspacePath(folder, change->target), latest) || latest != change->external) {
			wxLogWarning(_("A file changed during validation. Review the selected files again."));
			return;
		}
	}
	for (const auto* change : selected) {
		if (!WorkspaceWrite(BaselinePath(folder, change->target) + ".before-apply", change->baseline)) {
			wxLogError(_("Could not write a workspace recovery snapshot. No changes were applied."));
			return;
		}
	}
	int baselineErrors = 0;
	for (const auto* change : selected) {
		const auto& target = change->target;
		if (!target.IsText()) {
			*target.script = *change->staged;
			if (target.type == SCRIPT_TYPE_FIELD)
				cddata->MarkDataFieldModified(target.index, CHUNK_TYPE_SCRIPT);
			else
				cddata->MarkDataEnemyModified(target.index, CHUNK_TYPE_SCRIPT);
		}
		const auto canonical = target.IsText() ? WorkspaceTextDump(target, texts.at(target.text)) : change->canonical;
		const bool metadataWritten = WriteWorkspaceMetadata(*cddata, folder, target);
		if (!WriteWorkspaceBaseline(folder, target, canonical, change->external) || !metadataWritten)
			++baselineErrors;
	}
	for (auto& text : texts)
		*text.first = std::move(text.second);
	for (const auto* change : selected)
		if (change->target.IsText())
			cddata->MarkDataTextModified(change->target.index, CHUNK_TYPE_TEXT);
	if (!texts.empty())
		cddata->DisplayCurrentData();
	if (diagnostics.warning_amount) {
		LogDialog warnings(parent, diagnostics);
		warnings.ShowModal();
	}
    if (action == wxID_APPLY && !baselineErrors) {
        exportProject();
        return;
    }
	wxMessageBox(baselineErrors
		? _("Changes applied in memory, but some workspace baselines could not be updated. Inspect the changes before saving.")
		: _("Changes applied to the open project. Inspect them in HWS, then Save Mod or export when ready."),
		_("Project Workspace"), wxOK | wxICON_INFORMATION, parent);
}


struct ModManagerClientData : public wxClientData {
	unsigned int type;
	unsigned int section;
	unsigned int index;
	Chunk_Type chunk_type;
	
	ModManagerClientData(unsigned int t, unsigned int sec, uint16_t obji = 0, Chunk_Type chkt = 0xFF) :
		type(t),
		section(sec),
		index(obji),
		chunk_type(chkt) {}
};

ToolModManager::ToolModManager(wxWindow* p) :
	ModManagerWindow(p), cddata(nullptr) {
	auto* row = m_buttonok->GetContainingSizer();
	const auto add = [&](const wxString& label, auto action) {
		auto* button = new wxButton(this, wxID_ANY, label);
		button->Bind(wxEVT_BUTTON, action);
		row->Insert(row->GetItemCount() - 1, button, 0, wxALL, 5);
	};
	add(_("Open Workspace"), [this](wxCommandEvent&) {
		const auto folder = cddata ? ProjectWorkspaceFolder(*cddata) : wxString();
		if (!folder.empty() && !wxDirExists(folder))
			SyncProjectWorkspace(*cddata);
		if (wxDirExists(folder))
			wxLaunchDefaultApplication(folder);
		else if (wxDirExists(ProjectWorkspaceRoot()))
			wxLaunchDefaultApplication(ProjectWorkspaceRoot());
	});
	GetSizer()->Fit(this);
	Layout();
	m_listtree->AppendColumn(wxEmptyString);
	m_listtree->Connect( wxEVT_TREELIST_ITEM_CHECKED, wxTreeListEventHandler( ToolModManager::OnTreeListCheck ), NULL, this );
}

ToolModManager::~ToolModManager() {
	m_listtree->Disconnect( wxEVT_TREELIST_ITEM_CHECKED, wxTreeListEventHandler( ToolModManager::OnTreeListCheck ), NULL, this );
}

int ToolModManager::ShowModal(CDDataStruct* data) {
	unsigned int i, j;
	cddata = data;
	wxTreeListItem topitem, curitem, lowitem;
	SaveSet* dataset = &cddata->saveset;
	topitem = m_listtree->AppendItem(m_listtree->GetRootItem(), _(L"Generic"));
	m_listtree->SetItemData(topitem, new ModManagerClientData(0, 0));
	if (cddata->saveset.sectionloaded[DATA_SECTION_SPELL]) {
		curitem = m_listtree->AppendItem(topitem, _(L"Spells"));
		m_listtree->SetItemData(curitem, new ModManagerClientData(1, DATA_SECTION_SPELL));
		if (cddata->saveset.sectionmodified[DATA_SECTION_SPELL]) m_listtree->CheckItem(curitem);
	}
	if (cddata->saveset.sectionloaded[DATA_SECTION_SUPPORT]) {
		curitem = m_listtree->AppendItem(topitem, _(L"Supporting Abilities"));
		m_listtree->SetItemData(curitem, new ModManagerClientData(1, DATA_SECTION_SUPPORT));
		if (cddata->saveset.sectionmodified[DATA_SECTION_SUPPORT]) m_listtree->CheckItem(curitem);
	}
	if (cddata->saveset.sectionloaded[DATA_SECTION_CMD]) {
		curitem = m_listtree->AppendItem(topitem, _(L"Commands"));
		m_listtree->SetItemData(curitem, new ModManagerClientData(1, DATA_SECTION_CMD));
		if (cddata->saveset.sectionmodified[DATA_SECTION_CMD]) m_listtree->CheckItem(curitem);
	}
	if (cddata->saveset.sectionloaded[DATA_SECTION_STAT]) {
		curitem = m_listtree->AppendItem(topitem, _(L"Stats"));
		m_listtree->SetItemData(curitem, new ModManagerClientData(1, DATA_SECTION_STAT));
		if (cddata->saveset.sectionmodified[DATA_SECTION_STAT]) m_listtree->CheckItem(curitem);
	}
	if (cddata->saveset.sectionloaded[DATA_SECTION_PARTY_SPECIAL]) {
		curitem = m_listtree->AppendItem(topitem, _(L"Party - Special"));
		m_listtree->SetItemData(curitem, new ModManagerClientData(1, DATA_SECTION_PARTY_SPECIAL));
		if (cddata->saveset.sectionmodified[DATA_SECTION_PARTY_SPECIAL]) m_listtree->CheckItem(curitem);
	}
	if (cddata->saveset.sectionloaded[DATA_SECTION_ITEM]) {
		curitem = m_listtree->AppendItem(topitem, _(L"Items"));
		m_listtree->SetItemData(curitem, new ModManagerClientData(1, DATA_SECTION_ITEM));
		if (cddata->saveset.sectionmodified[DATA_SECTION_ITEM]) m_listtree->CheckItem(curitem);
	}
	if (cddata->saveset.sectionloaded[DATA_SECTION_SHOP]) {
		curitem = m_listtree->AppendItem(topitem, _(L"Shops"));
		m_listtree->SetItemData(curitem, new ModManagerClientData(1, DATA_SECTION_SHOP));
		if (cddata->saveset.sectionmodified[DATA_SECTION_SHOP]) m_listtree->CheckItem(curitem);
	}
	if (cddata->saveset.sectionloaded[DATA_SECTION_CARD]) {
		curitem = m_listtree->AppendItem(topitem, _(L"Tetra Master"));
		m_listtree->SetItemData(curitem, new ModManagerClientData(1, DATA_SECTION_CARD));
		if (cddata->saveset.sectionmodified[DATA_SECTION_CARD]) m_listtree->CheckItem(curitem);
	}
	if (cddata->saveset.sectionloaded[DATA_SECTION_SPELL_ANIM]) {
		curitem = m_listtree->AppendItem(topitem, _(L"Spell Animations"));
		m_listtree->SetItemData(curitem, new ModManagerClientData(1, DATA_SECTION_SPELL_ANIM));
		for (i = 0; i < cddata->spellanimset.amount; i++)
			if (!cddata->spellanimset.spell[i].is_empty) {
				for (j = 0; j < HADES_STRING_SPELL_MODEL.size(); j++)
					if (HADES_STRING_SPELL_MODEL[j].id == i) {
						lowitem = m_listtree->AppendItem(curitem, _(HADES_STRING_SPELL_MODEL[j].label));
						break;
					}
				if (j == HADES_STRING_SPELL_MODEL.size())
					lowitem = m_listtree->AppendItem(curitem, wxString::Format(wxT("Spell Animation %u"), i));
				m_listtree->SetItemData(lowitem, new ModManagerClientData(2, DATA_SECTION_SPELL_ANIM, i));
				if (cddata->spellanimset.spell[i].modified_data != 0) m_listtree->CheckItem(lowitem);
			}
		if (!m_listtree->AreAllChildrenInState(curitem, wxCHK_UNCHECKED))
			m_listtree->CheckItem(curitem);
	}
	if (!m_listtree->AreAllChildrenInState(topitem, wxCHK_UNCHECKED))
		m_listtree->CheckItem(topitem);
	if (cddata->saveset.sectionloaded[DATA_SECTION_ENMY]) {
		topitem = m_listtree->AppendItem(m_listtree->GetRootItem(), _(L"Enemies"));
		m_listtree->SetItemData(topitem, new ModManagerClientData(0, 1));
		for (i = 0; i < dataset->enemyset->battle_amount; i++) {
			curitem = m_listtree->AppendItem(topitem, _(dataset->enemyset->battle_name[i]));
			m_listtree->SetItemData(curitem, new ModManagerClientData(2, DATA_SECTION_ENMY, i));
			lowitem = m_listtree->AppendItem(curitem, _(L"Enemy Data"));
			m_listtree->SetItemData(lowitem, new ModManagerClientData(3, DATA_SECTION_ENMY, i, CHUNK_TYPE_ENEMY_STATS));
			if (dataset->enemyset->battle[i]->modified) m_listtree->CheckItem(lowitem);
			lowitem = m_listtree->AppendItem(curitem, _(L"Script"));
			m_listtree->SetItemData(lowitem, new ModManagerClientData(3, DATA_SECTION_ENMY, i, CHUNK_TYPE_SCRIPT));
			if (dataset->enemyset->script[i]->modified) m_listtree->CheckItem(lowitem);
			lowitem = m_listtree->AppendItem(curitem, _(L"Texts"));
			m_listtree->SetItemData(lowitem, new ModManagerClientData(3, DATA_SECTION_ENMY, i, CHUNK_TYPE_TEXT));
			if (dataset->enemyset->text[i]->modified) m_listtree->CheckItem(lowitem);
			if (GetGameType() == GAME_TYPE_PSX) {
				lowitem = m_listtree->AppendItem(curitem, _(L"Preloading"));
				m_listtree->SetItemData(lowitem, new ModManagerClientData(3, DATA_SECTION_ENMY, i, CHUNK_TYPE_IMAGE_MAP));
				if (dataset->enemyset->preload[i]->modified) m_listtree->CheckItem(lowitem);
			}
			lowitem = m_listtree->AppendItem(curitem, _(L"Animations & Cameras"));
			m_listtree->SetItemData(lowitem, new ModManagerClientData(3, DATA_SECTION_ENMY, i, CHUNK_TYPE_BATTLE_DATA));
			if (dataset->enemyset->battle_data[i]->modified) m_listtree->CheckItem(lowitem);
			if (!m_listtree->AreAllChildrenInState(curitem, wxCHK_UNCHECKED))
				m_listtree->CheckItem(curitem);
		}
		if (!m_listtree->AreAllChildrenInState(topitem, wxCHK_UNCHECKED))
			m_listtree->CheckItem(topitem);
	}
	if (cddata->saveset.sectionloaded[DATA_SECTION_TEXT]) {
		topitem = m_listtree->AppendItem(m_listtree->GetRootItem(), _(L"Texts"));
		m_listtree->SetItemData(topitem, new ModManagerClientData(0, 2));
		for (i = 0; i < dataset->textset->amount; i++) {
			curitem = m_listtree->AppendItem(topitem, _(dataset->textset->name[i]));
			m_listtree->SetItemData(curitem, new ModManagerClientData(2, DATA_SECTION_TEXT, i));
			if (dataset->textset->text_data[i] != NULL) {
				lowitem = m_listtree->AppendItem(curitem, _(L"Texts"));
				m_listtree->SetItemData(lowitem, new ModManagerClientData(3, DATA_SECTION_TEXT, i, CHUNK_TYPE_TEXT));
				if (dataset->textset->text_data[i]->modified) m_listtree->CheckItem(lowitem);
			}
			if (GetGameType() == GAME_TYPE_PSX && dataset->textset->charmap[i] != NULL) {
				lowitem = m_listtree->AppendItem(curitem, _(L"Charmap"));
				m_listtree->SetItemData(lowitem, new ModManagerClientData(3, DATA_SECTION_TEXT, i, CHUNK_TYPE_CHARMAP));
				if (dataset->textset->charmap[i]->modified) m_listtree->CheckItem(lowitem);
			}
			if (GetGameType() == GAME_TYPE_PSX && dataset->textset->chartim[i] != NULL) {
				lowitem = m_listtree->AppendItem(curitem, _(L"Texture"));
				m_listtree->SetItemData(lowitem, new ModManagerClientData(3, DATA_SECTION_TEXT, i, CHUNK_TYPE_TIM));
				if (dataset->textset->chartim[i]->parent_chunk->modified) m_listtree->CheckItem(lowitem);
			}
			if (!m_listtree->AreAllChildrenInState(curitem, wxCHK_UNCHECKED))
				m_listtree->CheckItem(curitem);
		}
		if (!m_listtree->AreAllChildrenInState(topitem, wxCHK_UNCHECKED))
			m_listtree->CheckItem(topitem);
	}
	if (cddata->saveset.sectionloaded[DATA_SECTION_WORLD_MAP]) {
		topitem = m_listtree->AppendItem(m_listtree->GetRootItem(), _(L"World Maps"));
		m_listtree->SetItemData(topitem, new ModManagerClientData(0, 3));
		curitem = m_listtree->AppendItem(topitem, _(L"Common Data"));
		m_listtree->SetItemData(curitem, new ModManagerClientData(1, DATA_SECTION_WORLD_MAP));
		if (dataset->worldset->world_data->modified) m_listtree->CheckItem(curitem);
		for (i = 0; i < dataset->worldset->amount; i++) {
			curitem = m_listtree->AppendItem(topitem, _(dataset->worldset->name[i]));
			m_listtree->SetItemData(curitem, new ModManagerClientData(2, DATA_SECTION_WORLD_MAP, i));
			lowitem = m_listtree->AppendItem(curitem, _(L"Script"));
			m_listtree->SetItemData(lowitem, new ModManagerClientData(3, DATA_SECTION_WORLD_MAP, i, CHUNK_TYPE_SCRIPT));
			if (dataset->worldset->script[i]->modified) m_listtree->CheckItem(lowitem);
			if (GetGameType() == GAME_TYPE_PSX || i == 0) {
				lowitem = m_listtree->AppendItem(curitem, _(L"Texts"));
				m_listtree->SetItemData(lowitem, new ModManagerClientData(3, DATA_SECTION_WORLD_MAP, i, CHUNK_TYPE_TEXT));
				if (dataset->worldset->text_data[i]->modified) m_listtree->CheckItem(lowitem);
			}
			if (GetGameType() == GAME_TYPE_PSX) {
				lowitem = m_listtree->AppendItem(curitem, _(L"Preloading"));
				m_listtree->SetItemData(lowitem, new ModManagerClientData(3, DATA_SECTION_WORLD_MAP, i, CHUNK_TYPE_IMAGE_MAP));
				if (dataset->worldset->preload[i]->modified) m_listtree->CheckItem(lowitem);
			}
			if (GetGameType() == GAME_TYPE_PSX && dataset->worldset->charmap[i] != NULL) {
				lowitem = m_listtree->AppendItem(curitem, _(L"Charmap"));
				m_listtree->SetItemData(lowitem, new ModManagerClientData(3, DATA_SECTION_WORLD_MAP, i, CHUNK_TYPE_CHARMAP));
				if (dataset->worldset->charmap[i]->modified) m_listtree->CheckItem(lowitem);
			}
			if (GetGameType() == GAME_TYPE_PSX && dataset->worldset->chartim[i] != NULL) {
				lowitem = m_listtree->AppendItem(curitem, _(L"Texture"));
				m_listtree->SetItemData(lowitem, new ModManagerClientData(3, DATA_SECTION_WORLD_MAP, i, CHUNK_TYPE_TIM));
				if (dataset->worldset->chartim[i]->parent_chunk->modified) m_listtree->CheckItem(lowitem);
			}
			if (!m_listtree->AreAllChildrenInState(curitem, wxCHK_UNCHECKED))
				m_listtree->CheckItem(curitem);
		}
		if (!m_listtree->AreAllChildrenInState(topitem, wxCHK_UNCHECKED))
			m_listtree->CheckItem(topitem);
	}
	if (cddata->saveset.sectionloaded[DATA_SECTION_FIELD]) {
		topitem = m_listtree->AppendItem(m_listtree->GetRootItem(), _(L"Fields"));
		m_listtree->SetItemData(topitem, new ModManagerClientData(0, 4));
		for (i = 0; i < dataset->fieldset->amount; i++) {
			curitem = m_listtree->AppendItem(topitem, _(dataset->fieldset->script_data[i]->name.str_nice));
			m_listtree->SetItemData(curitem, new ModManagerClientData(2, DATA_SECTION_FIELD, i));
			lowitem = m_listtree->AppendItem(curitem, _(L"Script"));
			m_listtree->SetItemData(lowitem, new ModManagerClientData(3, DATA_SECTION_FIELD, i, CHUNK_TYPE_SCRIPT));
			if (dataset->fieldset->script_data[i]->modified) m_listtree->CheckItem(lowitem);
			if (dataset->fieldset->preload[i] != NULL) {
				lowitem = m_listtree->AppendItem(curitem, _(L"Preloading"));
				m_listtree->SetItemData(lowitem, new ModManagerClientData(3, DATA_SECTION_FIELD, i, CHUNK_TYPE_IMAGE_MAP));
				if (dataset->fieldset->preload[i]->modified) m_listtree->CheckItem(lowitem);
			}
			lowitem = m_listtree->AppendItem(curitem, _(L"Model Role"));
			m_listtree->SetItemData(lowitem, new ModManagerClientData(3, DATA_SECTION_FIELD, i, CHUNK_TYPE_FIELD_ROLE));
			if (dataset->fieldset->role[i]->modified) m_listtree->CheckItem(lowitem);
			if (dataset->fieldset->background_data[i] != NULL) {
				lowitem = m_listtree->AppendItem(curitem, _(L"Background"));
				m_listtree->SetItemData(lowitem, new ModManagerClientData(3, DATA_SECTION_FIELD, i, CHUNK_TYPE_FIELD_TILES));
				if (dataset->fieldset->background_data[i]->modified) m_listtree->CheckItem(lowitem);
			}
			if (dataset->fieldset->walkmesh[i] != NULL) {
				lowitem = m_listtree->AppendItem(curitem, _(L"Walkmesh"));
				m_listtree->SetItemData(lowitem, new ModManagerClientData(3, DATA_SECTION_FIELD, i, CHUNK_TYPE_FIELD_WALK));
				if (dataset->fieldset->walkmesh[i]->modified) m_listtree->CheckItem(lowitem);
			}
			if (!m_listtree->AreAllChildrenInState(curitem, wxCHK_UNCHECKED))
				m_listtree->CheckItem(curitem);
		}
		if (!m_listtree->AreAllChildrenInState(topitem, wxCHK_UNCHECKED))
			m_listtree->CheckItem(topitem);
	}
	if (cddata->saveset.sectionloaded[DATA_SECTION_BATTLE_SCENE]) {
		topitem = m_listtree->AppendItem(m_listtree->GetRootItem(), _(L"Battle Scenes"));
		m_listtree->SetItemData(topitem, new ModManagerClientData(0, 5));
		for (i = 0; i < dataset->sceneset->scene_amount; i++) {
			curitem = m_listtree->AppendItem(topitem, _(dataset->sceneset->scene_name[i]));
			m_listtree->SetItemData(curitem, new ModManagerClientData(2, DATA_SECTION_BATTLE_SCENE, i));
			lowitem = m_listtree->AppendItem(curitem, _(L"Model"));
			m_listtree->SetItemData(lowitem, new ModManagerClientData(3, DATA_SECTION_BATTLE_SCENE, i, CHUNK_TYPE_BATTLE_SCENE));
			if (dataset->sceneset->scene[i]->modified) m_listtree->CheckItem(lowitem);
			if (dataset->sceneset->image[i] != NULL) {
				lowitem = m_listtree->AppendItem(curitem, _(L"Textures"));
				m_listtree->SetItemData(lowitem, new ModManagerClientData(3, DATA_SECTION_BATTLE_SCENE, i, CHUNK_TYPE_TIM));
				if (dataset->sceneset->image[i]->parent_chunk->modified) m_listtree->CheckItem(lowitem);
			}
			if (!m_listtree->AreAllChildrenInState(curitem, wxCHK_UNCHECKED))
				m_listtree->CheckItem(curitem);
		}
		if (!m_listtree->AreAllChildrenInState(topitem, wxCHK_UNCHECKED))
			m_listtree->CheckItem(topitem);
	}
	if (cddata->saveset.sectionloaded[DATA_SECTION_MENU_UI]) {
		topitem = m_listtree->AppendItem(m_listtree->GetRootItem(), _(L"Interface"));
		m_listtree->SetItemData(topitem, new ModManagerClientData(0, 6));
		curitem = m_listtree->AppendItem(topitem, _(L"UI Texts"));
		m_listtree->SetItemData(curitem, new ModManagerClientData(1, DATA_SECTION_MENU_UI));
		if (dataset->ffuiset->special_text->modified) m_listtree->CheckItem(curitem);
		if (!m_listtree->AreAllChildrenInState(topitem, wxCHK_UNCHECKED))
			m_listtree->CheckItem(topitem);
	}
	if (cddata->saveset.sectionloaded[DATA_SECTION_ASSEMBLY]) {
		if (cddata->gametype == GAME_TYPE_PSX) {
			topitem = m_listtree->AppendItem(m_listtree->GetRootItem(), _(L"MIPS Code"));
			m_listtree->SetItemData(topitem, new ModManagerClientData(0, 7));
			curitem = m_listtree->AppendItem(topitem, _(L"Battle MIPS"));
			m_listtree->SetItemData(curitem, new ModManagerClientData(1, DATA_SECTION_MIPS));
			if (cddata->mipsset.battle_modified) m_listtree->CheckItem(curitem);
			if (!m_listtree->AreAllChildrenInState(topitem, wxCHK_UNCHECKED))
				m_listtree->CheckItem(topitem);
		} else {
			topitem = m_listtree->AppendItem(m_listtree->GetRootItem(), _(L"CIL Code"));
			m_listtree->SetItemData(topitem, new ModManagerClientData(0, 7));
			if (cddata->saveset.sectionmodified[DATA_SECTION_ASSEMBLY]) m_listtree->CheckItem(topitem);
		}
	}
	return ModManagerWindow::ShowModal();
}

void ToolModMarkModification(CDDataStruct* data, ModManagerClientData* info, bool check) {
	unsigned int i;
	if (info->type==0) {
		if (!check) {
			if (info->section==1)
				data->saveset.sectionmodified[DATA_SECTION_ENMY] = false;
			else if (info->section==2)
				data->saveset.sectionmodified[DATA_SECTION_TEXT] = false;
			else if (info->section==3)
				data->saveset.sectionmodified[DATA_SECTION_WORLD_MAP] = false;
			else if (info->section==4)
				data->saveset.sectionmodified[DATA_SECTION_FIELD] = false;
			else if (info->section==5)
				data->saveset.sectionmodified[DATA_SECTION_BATTLE_SCENE] = false;
			else if (info->section==6)
				data->saveset.sectionmodified[DATA_SECTION_MENU_UI] = false;
			else if (info->section==7) {
				if (GetGameType()==GAME_TYPE_PSX)
					data->saveset.sectionmodified[DATA_SECTION_ASSEMBLY] = false;
			}
		}
		if (info->section==7 && GetGameType()!=GAME_TYPE_PSX)
			data->saveset.sectionmodified[DATA_SECTION_ASSEMBLY] = check;
	} else if (info->type==1) {
		if (info->section==DATA_SECTION_SPELL) {
			data->saveset.sectionmodified[DATA_SECTION_SPELL] = check;
		} else if (info->section==DATA_SECTION_SUPPORT) {
			data->saveset.sectionmodified[DATA_SECTION_SUPPORT] = check;
		} else if (info->section==DATA_SECTION_CMD) {
			data->saveset.sectionmodified[DATA_SECTION_CMD] = check;
		} else if (info->section==DATA_SECTION_STAT) {
			data->saveset.sectionmodified[DATA_SECTION_STAT] = check;
		} else if (info->section==DATA_SECTION_PARTY_SPECIAL) {
			data->saveset.sectionmodified[DATA_SECTION_PARTY_SPECIAL] = check;
		} else if (info->section==DATA_SECTION_ITEM) {
			data->saveset.sectionmodified[DATA_SECTION_ITEM] = check;
		} else if (info->section==DATA_SECTION_SHOP) {
			data->saveset.sectionmodified[DATA_SECTION_SHOP] = check;
		} else if (info->section==DATA_SECTION_CARD) {
			data->saveset.sectionmodified[DATA_SECTION_CARD] = check;
		} else if (info->section==DATA_SECTION_SPELL_ANIM) {
			data->saveset.sectionmodified[DATA_SECTION_SPELL_ANIM] = check;
		} else if (info->section==DATA_SECTION_WORLD_MAP) {
			if (check) {
				data->worldset.world_data->MarkDataModified();
				data->saveset.sectionmodified[DATA_SECTION_WORLD_MAP] = true;
			} else {
				data->worldset.world_data->modified = false;
				data->worldset.world_data->parent_chunk->modified = false;
			}
		} else if (info->section==DATA_SECTION_MENU_UI) {
			data->ffuiset.special_text->modified = check;
			if (check)
				data->saveset.sectionmodified[DATA_SECTION_MENU_UI] = true;
		} else if (info->section==DATA_SECTION_ASSEMBLY) {
			data->mipsset.battle_modified = check;
			if (check)
				data->saveset.sectionmodified[DATA_SECTION_ASSEMBLY] = true;
		}
	} else if (info->type==2) {
		if (!check) {
			if (info->section==DATA_SECTION_ENMY) {
				data->enemyset.battle[info->index]->parent_cluster->modified = false;
			} else if (info->section==DATA_SECTION_TEXT) {
				if (data->textset.text_data[info->index]!=NULL)
					data->textset.text_data[info->index]->parent_cluster->modified = false;
				else
					data->textset.charmap[info->index]->parent_cluster->modified = false;
			} else if (info->section==DATA_SECTION_WORLD_MAP) {
				data->worldset.script[info->index]->parent_cluster->modified = false;
			} else if (info->section==DATA_SECTION_FIELD) {
				data->fieldset.script_data[info->index]->parent_cluster->modified = false;
			} else if (info->section==DATA_SECTION_BATTLE_SCENE) {
				data->sceneset.scene[info->index]->parent_cluster->modified = false;
			} else if (info->section==DATA_SECTION_SPELL_ANIM) {
				data->spellanimset.spell[info->index].modified_data = 0;
			}
		} else {
			if (info->section==DATA_SECTION_SPELL_ANIM) {
				data->spellanimset.spell[info->index].modified_data = SPELL_ANIMATION_DATA_TYPE_WHOLE;
			}
		}
	} else if (info->type==3) {
		if (info->section==DATA_SECTION_ENMY) {
			if (check)
				data->saveset.sectionmodified[DATA_SECTION_ENMY] = true;
			if (info->chunk_type==CHUNK_TYPE_ENEMY_STATS) {
				if (check) {
					data->enemyset.battle[info->index]->MarkDataModified();
				} else {
					data->enemyset.battle[info->index]->modified = false;
					data->enemyset.battle[info->index]->parent_chunk->modified = false;
				}
			} else if (info->chunk_type==CHUNK_TYPE_SCRIPT) {
				if (check) {
					data->enemyset.script[info->index]->MarkDataModified();
				} else {
					data->enemyset.script[info->index]->modified = false;
					data->enemyset.script[info->index]->parent_chunk->modified = false;
				}
			} else if (info->chunk_type==CHUNK_TYPE_TEXT) {
				if (check) {
					data->enemyset.text[info->index]->MarkDataModified();
				} else {
					data->enemyset.text[info->index]->modified = false;
					data->enemyset.text[info->index]->parent_chunk->modified = false;
				}
			} else if (info->chunk_type==CHUNK_TYPE_IMAGE_MAP) {
				if (check) {
					data->enemyset.preload[info->index]->MarkDataModified();
				} else {
					data->enemyset.preload[info->index]->modified = false;
					data->enemyset.preload[info->index]->parent_chunk->modified = false;
				}
			} else if (info->chunk_type==CHUNK_TYPE_BATTLE_DATA) {
				if (check) {
					data->enemyset.battle_data[info->index]->MarkDataModified();
				} else {
					data->enemyset.battle_data[info->index]->modified = false;
					data->enemyset.battle_data[info->index]->parent_chunk->modified = false;
				}
			}
		} else if (info->section==DATA_SECTION_TEXT) {
			if (check)
				data->saveset.sectionmodified[DATA_SECTION_TEXT] = true;
			if (info->chunk_type==CHUNK_TYPE_TEXT) {
				if (check) {
					data->textset.text_data[info->index]->MarkDataModified();
				} else {
					data->textset.text_data[info->index]->modified = false;
					data->textset.text_data[info->index]->parent_chunk->modified = false;
				}
			} else if (info->chunk_type==CHUNK_TYPE_CHARMAP) {
				if (check) {
					data->textset.charmap[info->index]->MarkDataModified();
				} else {
					data->textset.charmap[info->index]->modified = false;
					data->textset.charmap[info->index]->parent_chunk->modified = false;
				}
			} else if (info->chunk_type==CHUNK_TYPE_TIM) {
				for (i=0;i<data->textset.tim_amount[info->index];i++) {
					if (check) {
						data->textset.chartim[info->index][i].MarkDataModified();
					} else {
						data->textset.chartim[info->index][i].modified = false;
						data->textset.chartim[info->index][i].parent_chunk->modified = false;
					}
				}
			}
		} else if (info->section==DATA_SECTION_WORLD_MAP) {
			if (check)
				data->saveset.sectionmodified[DATA_SECTION_WORLD_MAP] = true;
			if (info->chunk_type==CHUNK_TYPE_SCRIPT) {
				if (check) {
					data->worldset.script[info->index]->MarkDataModified();
				} else {
					data->worldset.script[info->index]->modified = false;
					data->worldset.script[info->index]->parent_chunk->modified = false;
				}
			} else if (info->chunk_type==CHUNK_TYPE_TEXT) {
				if (check) {
					data->worldset.text_data[info->index]->MarkDataModified();
				} else {
					data->worldset.text_data[info->index]->modified = false;
					data->worldset.text_data[info->index]->parent_chunk->modified = false;
				}
			} else if (info->chunk_type==CHUNK_TYPE_IMAGE_MAP) {
				if (check) {
					data->worldset.preload[info->index]->MarkDataModified();
				} else {
					data->worldset.preload[info->index]->modified = false;
					data->worldset.preload[info->index]->parent_chunk->modified = false;
				}
			} else if (info->chunk_type==CHUNK_TYPE_CHARMAP) {
				if (check) {
					data->worldset.charmap[info->index]->MarkDataModified();
				} else {
					data->worldset.charmap[info->index]->modified = false;
					data->worldset.charmap[info->index]->parent_chunk->modified = false;
				}
			} else if (info->chunk_type==CHUNK_TYPE_TIM) {
				for (i=0;i<data->worldset.tim_amount[info->index];i++) {
					if (check) {
						data->worldset.chartim[info->index][i].MarkDataModified();
					} else {
						data->worldset.chartim[info->index][i].modified = false;
						data->worldset.chartim[info->index][i].parent_chunk->modified = false;
					}
				}
			}
		} else if (info->section==DATA_SECTION_FIELD) {
			if (check)
				data->saveset.sectionmodified[DATA_SECTION_FIELD] = true;
			if (info->chunk_type==CHUNK_TYPE_SCRIPT) {
				if (check) {
					data->fieldset.script_data[info->index]->MarkDataModified();
				} else {
					data->fieldset.script_data[info->index]->modified = false;
					data->fieldset.script_data[info->index]->parent_chunk->modified = false;
				}
			} else if (info->chunk_type==CHUNK_TYPE_IMAGE_MAP) {
				if (check) {
					data->fieldset.preload[info->index]->MarkDataModified();
				} else {
					data->fieldset.preload[info->index]->modified = false;
					data->fieldset.preload[info->index]->parent_chunk->modified = false;
				}
			} else if (info->chunk_type==CHUNK_TYPE_FIELD_ROLE) {
				if (check) {
					data->fieldset.role[info->index]->MarkDataModified();
				} else {
					data->fieldset.role[info->index]->modified = false;
					data->fieldset.role[info->index]->parent_chunk->modified = false;
				}
			} else if (info->chunk_type == CHUNK_TYPE_FIELD_TILES) {
				if (check) {
					data->fieldset.background_data[info->index]->MarkDataModified();
				} else {
					data->fieldset.background_data[info->index]->modified = false;
					data->fieldset.background_data[info->index]->parent_chunk->modified = false;
				}
			} else if (info->chunk_type == CHUNK_TYPE_FIELD_WALK) {
				if (check) {
					data->fieldset.walkmesh[info->index]->MarkDataModified();
				} else {
					data->fieldset.walkmesh[info->index]->modified = false;
					data->fieldset.walkmesh[info->index]->parent_chunk->modified = false;
				}
			}
		} else if (info->section==DATA_SECTION_BATTLE_SCENE) {
			if (check)
				data->saveset.sectionmodified[DATA_SECTION_BATTLE_SCENE] = true;
			if (info->chunk_type==CHUNK_TYPE_BATTLE_SCENE) {
				if (check) {
					data->sceneset.scene[info->index]->MarkDataModified();
				} else {
					data->sceneset.scene[info->index]->modified = false;
					data->sceneset.scene[info->index]->parent_chunk->modified = false;
				}
			} else if (info->chunk_type==CHUNK_TYPE_TIM) {
				if (check) {
					data->sceneset.image[info->index]->MarkDataModified();
				} else {
					data->sceneset.image[info->index]->modified = false;
					data->sceneset.image[info->index]->parent_chunk->modified = false;
				}
			}
		}
	}
}

void ToolModManager::OnTreeListCheck(wxTreeListEvent& event) {
	wxTreeListItem item = event.GetItem();
	wxCheckBoxState checkstate = m_listtree->GetCheckedState(item);
	ToolModMarkModification(cddata,(ModManagerClientData*)m_listtree->GetItemData(item),checkstate==wxCHK_CHECKED);
	for (wxTreeListItem child = m_listtree->GetFirstChild(item);child.IsOk();child = m_listtree->GetNextSibling(child)) {
		for (wxTreeListItem cchild = m_listtree->GetFirstChild(child);cchild.IsOk();cchild = m_listtree->GetNextSibling(cchild)) {
			m_listtree->CheckItem(cchild,checkstate);
			ToolModMarkModification(cddata,(ModManagerClientData*)m_listtree->GetItemData(cchild),checkstate==wxCHK_CHECKED);
		}
		m_listtree->CheckItem(child,checkstate);
		ToolModMarkModification(cddata,(ModManagerClientData*)m_listtree->GetItemData(child),checkstate==wxCHK_CHECKED);
	}
}

void ToolModManager::OnButtonClick(wxCommandEvent& event) {
	EndModal(event.GetId());
}
