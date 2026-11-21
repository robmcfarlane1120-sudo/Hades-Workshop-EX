// Modified by Cheddarbung - November 2026. Based on Hades Workshop 0.53 by Tirlititi. Distributed under GNU GPL v3.

#pragma once

#include <wx/combobox.h>
#include <wx/listctrl.h>
#include <wx/weakref.h>
#include <wx/eventfilter.h>
#include <windows.h>
#include <wx/treelist.h>
#include <vector>

class ListPrefixSearch final : public wxEventFilter {
	wxWeakRef<wxWindow> target;
	int anchor = 0;
	bool chord = false;
	HHOOK nativeHook = nullptr;
	static ListPrefixSearch*& Instance() {
		static ListPrefixSearch* instance = nullptr;
		return instance;
	}

	static LRESULT CALLBACK NativeDropdownKeys(int hookCode, WPARAM removed, LPARAM data) {
		if (hookCode >= 0 && removed == PM_REMOVE && Instance()) {
			auto* message = reinterpret_cast<MSG*>(data);
			if (message->message == WM_KEYDOWN || message->message == WM_KEYUP) {
				auto* focus = wxWindow::FindFocus();
				auto* choice = dynamic_cast<wxChoice*>(focus);
				auto* combo = dynamic_cast<wxComboBox*>(focus);
				// Native expanded combo boxes run a modal message loop which
				// bypasses wxEVT_CHAR_HOOK. Handle those keys before translation.
				if ((choice || combo) && SendMessage(reinterpret_cast<HWND>(focus->GetHandle()), CB_GETDROPPEDSTATE, 0, 0)) {
					wxKeyEvent key(message->message == WM_KEYDOWN ? wxEVT_CHAR_HOOK : wxEVT_KEY_UP);
					key.m_keyCode = static_cast<int>(message->wParam);
					key.SetControlDown((GetKeyState(VK_CONTROL) & 0x8000) != 0);
					key.SetAltDown((GetKeyState(VK_MENU) & 0x8000) != 0);
					key.SetShiftDown((GetKeyState(VK_SHIFT) & 0x8000) != 0);
					key.SetEventObject(focus);
					if (Instance()->FilterEvent(key) == Event_Processed)
						message->message = WM_NULL;
				}
			}
		}
		return CallNextHookEx(nullptr, hookCode, removed, data);
	}

	static wxString SearchName(wxString name) {
		name.Trim(false);
		// Ignore leading HWS text-control tags, e.g. [STRT=47,1].
		while (name.StartsWith("[")) {
			const int end = name.Find(']');
			if (end == wxNOT_FOUND)
				break;
			name = name.Mid(end + 1).Trim(false);
		}
		size_t offset = 0;
		while (offset < name.length() && name[offset] >= '0' && name[offset] <= '9')
			offset++;
		if (offset != 0) {
			while (offset < name.length() && name[offset] == ' ')
				offset++;
			if (offset < name.length() && (name[offset] == ':' || name[offset] == '-'))
				return name.Mid(offset + 1).Trim(false);
		}
		return name;
	}

public:
	ListPrefixSearch() {
		Instance() = this;
		nativeHook = SetWindowsHookEx(WH_GETMESSAGE, NativeDropdownKeys, nullptr, GetCurrentThreadId());
	}
	~ListPrefixSearch() override {
		if (nativeHook)
			UnhookWindowsHookEx(nativeHook);
		Instance() = nullptr;
	}
	int FilterEvent(wxEvent& event) override {
		const auto type = event.GetEventType();
		if (type != wxEVT_CHAR_HOOK && type != wxEVT_KEY_UP && type != wxEVT_KILL_FOCUS)
			return Event_Skip;
		if (type == wxEVT_KILL_FOCUS) {
			if (event.GetEventObject() == target.get())
				anchor = 0;
			return Event_Skip;
		}

		auto& key = static_cast<wxKeyEvent&>(event);
		int code = key.GetKeyCode();
		if (type == wxEVT_KEY_UP) {
			if (code == anchor)
				anchor = 0;
			return Event_Skip;
		}

		wxWindow* focus = wxWindow::FindFocus();
		wxTreeListCtrl* tree = nullptr;
		for (wxWindow* owner = focus; owner && !tree; owner = owner->GetParent())
			tree = dynamic_cast<wxTreeListCtrl*>(owner);
		if (tree)
			focus = tree;
		auto* list = dynamic_cast<wxListBox*>(focus);
		auto* choice = dynamic_cast<wxChoice*>(focus);
		auto* combo = dynamic_cast<wxComboBox*>(focus);
		auto* report = dynamic_cast<wxListCtrl*>(focus);
		wxItemContainer* items = nullptr;
		wxEventType selectionEvent = wxEVT_NULL;
		if (list && !list->HasMultipleSelection()) {
			items = list;
			selectionEvent = wxEVT_LISTBOX;
		} else if (choice && SendMessage(reinterpret_cast<HWND>(choice->GetHandle()), CB_GETDROPPEDSTATE, 0, 0)) {
			items = choice;
			selectionEvent = wxEVT_CHOICE;
		} else if (combo && SendMessage(reinterpret_cast<HWND>(combo->GetHandle()), CB_GETDROPPEDSTATE, 0, 0)) {
			items = combo;
			selectionEvent = wxEVT_COMBOBOX;
		}

		if ((!tree && !items && (!report || !report->HasFlag(wxLC_SINGLE_SEL))) ||
		    key.ControlDown() || key.AltDown() || key.MetaDown() ||
		    !((code >= 'A' && code <= 'Z') || (!key.ShiftDown() && code >= '0' && code <= '9'))) {
			anchor = 0;
			return Event_Skip;
		}
		std::vector<wxTreeListItem> visible;
		int treeCurrent = -1;
		if (tree) {
			wxTreeListItems selections;
			tree->GetSelections(selections);
			for (auto item = tree->GetFirstItem(); item.IsOk(); item = tree->GetNextItem(item)) {
				bool shown = true;
				for (auto parent = tree->GetItemParent(item); parent.IsOk() && parent != tree->GetRootItem(); parent = tree->GetItemParent(parent)) {
					if (!tree->IsExpanded(parent)) { shown = false; break; }
				}
				if (!shown) continue;
				if (!selections.empty() && selections.front() == item)
					treeCurrent = static_cast<int>(visible.size());
				visible.push_back(item);
			}
		}
		const int count = tree ? static_cast<int>(visible.size()) : (items ? items->GetCount() : report->GetItemCount());
		if (!count)
			return Event_Skip;
		if (focus != target.get() || (anchor && !wxGetKeyState(static_cast<wxKeyCode>(anchor))))
			anchor = 0;
		if (anchor == 0) {
			target = focus;
			anchor = code;
			chord = false;
		}
		wxString prefix(static_cast<wxChar>(anchor));
		if (code != anchor) {
			prefix += static_cast<wxChar>(code);
			chord = true;
		} else if (chord) {
			return Event_Processed;
		}

		// GetSelection() reports the committed item while a wxChoice is open.
		// Cycling must start at the live highlighted row instead.
		const int current = tree ? treeCurrent : choice ? choice->GetCurrentSelection() :
			(items ? items->GetSelection() :
			report->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED));
		for (int step = 1; step <= count; step++) {
			const int index = (current + step) % count;
			wxString label = tree ? tree->GetItemText(visible[index]) : (items ? items->GetString(index) : report->GetItemText(index));
			if (SearchName(label).Left(prefix.length()).CmpNoCase(prefix) != 0)
				continue;
			if (index != current) {
				if (tree) {
					tree->UnselectAll();
					tree->Select(visible[index]);
					tree->EnsureVisible(visible[index]);
				} else if (items) {
					items->SetSelection(index);
					wxCommandEvent selected(selectionEvent, focus->GetId());
					selected.SetEventObject(focus);
					selected.SetInt(index);
					selected.SetString(label);
					if (items->HasClientObjectData())
						selected.SetClientObject(items->GetClientObject(index));
					else if (items->HasClientUntypedData())
						selected.SetClientData(items->GetClientData(index));
					focus->ProcessWindowEvent(selected);
				} else {
					report->EnsureVisible(index);
					report->SetItemState(index, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
						wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
				}
			}
			break;
		}
		return Event_Processed;
	}
};
