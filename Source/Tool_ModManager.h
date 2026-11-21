#ifndef _TOOL_MODMANAGER_H
#define _TOOL_MODMANAGER_H

class ToolModManager;

#include "Configuration.h"
#include "Gui_Manipulation.h"
#include "gui.h"

class ToolModManager : public ModManagerWindow {
public:
	CDDataStruct* cddata;
	
	ToolModManager(wxWindow* p);
	~ToolModManager();
	int ShowModal(CDDataStruct* data);
	static void SyncProjectWorkspace(CDDataStruct& project);
	static bool SyncTextBlockWorkspace(CDDataStruct& project, unsigned int index, wxWindow* parent = nullptr);
	static bool SyncFieldWorkspace(CDDataStruct& project, int fieldId, ScriptDataStruct& script, wxWindow* parent = nullptr);
	static void CreateWorkspaceRoot();
	static bool HasPendingWorkspaceChanges(const CDDataStruct& project);
	static void ReviewWorkspace(CDDataStruct& project, wxWindow* parent);

private:
	wxMenu* list_popup_menu;
	
	void OnTreeListCheck(wxTreeListEvent& event);
	void OnButtonClick(wxCommandEvent& event);
};

#endif
