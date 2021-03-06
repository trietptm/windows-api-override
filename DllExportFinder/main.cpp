/*
Copyright (C) 2004 Jacquelin POTIER <jacquelin.potier@free.fr>
Dynamic aspect ratio code Copyright (C) 2004 Jacquelin POTIER <jacquelin.potier@free.fr>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; version 2 of the License.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

#include <Windows.h>
#pragma warning (push)
#pragma warning(disable : 4005)// for '_stprintf' : macro redefinition in tchar.h
#include <TCHAR.h>
#pragma warning (pop)
#include <malloc.h>
#include <Dbghelp.h>
#pragma comment (lib,"Dbghelp.lib")
#include "resource.h"

#include "about.h"
#include "../Tools/GUI/Dialog/DialogHelper.h"
#include "../Tools/GUI/ListView/ListView.h"
#include "../Tools/GUI/ToolBar/Toolbar.h"
#include "../Tools/File/FileSearch.h"
#include "../Tools/Pe/PE.h"
#include "../Tools/File/StdFileOperations.h"
#include "../Tools/String/WildCharCompare.h"
#include "../Tools/Gui/BrowseForFolder/BrowseForFolder.h"
#include "../Tools/Gui/Statusbar/Statusbar.h"
#include "../Tools/String/MultipleElementsParsing.h"

#define MAIN_DIALOG_MIN_WIDTH 520
#define MAIN_DIALOG_MIN_HEIGHT 200
#define TIMEOUT_CANCEL_SEARCH_THREAD 5000 // max time in ms to wait for the cancel event to be taken into account
#define NB_COLUMNS 4
CListview::COLUMN_INFO pColumnInfo[NB_COLUMNS]={
                                                {_T("Function Name"),150,LVCFMT_LEFT},
                                                {_T("Undecorated Name"),150,LVCFMT_LEFT},
                                                {_T("File Name"),120,LVCFMT_LEFT},
                                                {_T("File Path"),240,LVCFMT_LEFT}
                                                };
enum tagListviewSearchResultColumnsIndex
{
    ListviewSearchResultColumnsIndex_FunctionName=0,
    ListviewSearchResultColumnsIndex_UndecoratedName=1,
    ListviewSearchResultColumnsIndex_FileName=2,
    ListviewSearchResultColumnsIndex_FilePath=3
};
#define HELP_FILE _T("DllExportFinder.chm")

HINSTANCE mhInstance;
HWND mhWndDialog=NULL;
CListview* pListView=NULL;
CToolbar* pToolbar=NULL;
CStatusbar* pStatusbar=NULL;
HANDLE hEvtCancel=NULL;
HANDLE hSearchingThread=NULL;
UINT MenuCopyFunctionNameIndex=0;
UINT MenuCopyDllNameIndex=0;
UINT MenuCopyDllPathIndex=0;

LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
void Init();
void Close();
void CheckSize(RECT* pWinRect);
void Resize();
BOOL IsSearching();


DWORD WINAPI FindExports(LPVOID lpParameter);
BOOL CallBackFileFound(TCHAR* Directory,WIN32_FIND_DATA* pWin32FindData,PVOID UserParam);

void OnAbout();
void OnHelp();
void OnDonation();
void OnStartStop();
void OnStart();
void OnCancel();
DWORD WINAPI CancelSearch(LPVOID lpParameter);
void CallBackPopUpMenuItem(UINT MenuID,LPVOID UserParam);
void SetToolbarMode(BOOL bIsSearching);

//-----------------------------------------------------------------------------
// Name: WinMain
// Object: Entry point of app
// Parameters :
//     in  : 
//     out :
//     return : 
//-----------------------------------------------------------------------------
int WINAPI WinMain(HINSTANCE hInstance,
                   HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine,
                   int nCmdShow
                   )
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(nCmdShow);
    UNREFERENCED_PARAMETER(lpCmdLine);

    mhInstance=hInstance;

    // enable Xp style
    InitCommonControls();

    hEvtCancel=CreateEvent(NULL,FALSE,FALSE,NULL);

    ///////////////////////////////////////////////////////////////////////////
    // show main window
    //////////////////////////////////////////////////////////////////////////
    DialogBox(hInstance, (LPCTSTR)IDD_DIALOG_DLL_EXPORT_FINDER, NULL, (DLGPROC)WndProc);

    CloseHandle(hEvtCancel);
}

//-----------------------------------------------------------------------------
// Name: BrowseForDirectory
// Object: show browse for folder dialog
// Parameters :
//     in  : HWND hResultWindow : window handle which text is going to be field by BrowseForDirectory
//     out :
//     return : 
//-----------------------------------------------------------------------------
void BrowseForDirectory(HWND hResultWindow)
{

    CBrowseForFolder BrowseForFolder;
    TCHAR CurrentPath[MAX_PATH];
    GetWindowText(hResultWindow,CurrentPath,MAX_PATH);
    if (*CurrentPath==0)
        ::GetCurrentDirectory(MAX_PATH,CurrentPath);
    if (BrowseForFolder.BrowseForFolder(mhWndDialog,CurrentPath,
                                        _T("Select Search Path"),
                                        BIF_EDITBOX | BIF_NEWDIALOGSTYLE
                                        )
        )
        SetWindowText(hResultWindow,BrowseForFolder.szSelectedPath);
}


//-----------------------------------------------------------------------------
// Name: Init
// Object: Initialize objects that requires the Dialog exists
//          and sets some dialog properties
// Parameters :
//     in  : 
//     out :
//     return : 
//-----------------------------------------------------------------------------
void Init()
{
    CDialogHelper::SetIcon(mhWndDialog,IDI_ICON_APP);

    pListView=new CListview(GetDlgItem(mhWndDialog,IDC_LIST_SEARCH_RESULTS));

    pListView->InitListViewColumns(NB_COLUMNS,pColumnInfo);
    pListView->SetStyle(TRUE,FALSE,FALSE,FALSE);

    pListView->SetSortingType(CListview::SortingTypeString);
    pListView->SetPopUpMenuItemClickCallback(CallBackPopUpMenuItem,NULL);

    MenuCopyFunctionNameIndex=pListView->pPopUpMenu->Add(_T("Copy Function Name"),(UINT)0);
    MenuCopyDllNameIndex=pListView->pPopUpMenu->Add(_T("Copy File Name"),1);
    MenuCopyDllPathIndex=pListView->pPopUpMenu->Add(_T("Copy File Full Path"),2);
    pListView->pPopUpMenu->AddSeparator(3);

    pToolbar=new CToolbar(mhInstance,mhWndDialog,TRUE,TRUE,24,24);
    pToolbar->AddButton(IDC_BUTTON_START_STOP,_T("Start"),IDI_ICON_START,IDI_ICON_START,IDI_ICON_START);
    pToolbar->AddCheckButton(IDC_BUTTON_INCLUDE_SUBDIR,IDI_ICON_SUBDIR,_T("Include Subdirectories"));
    pToolbar->AddSeparator();
    pToolbar->AddButton(IDC_BUTTON_SAVE,_T("Save"),IDI_ICON_SAVE,_T("Save Results"));
    pToolbar->AddSeparator();
    pToolbar->AddButton(IDC_BUTTON_HELP,IDI_ICON_HELP,_T("Help"));
    pToolbar->AddButton(IDC_BUTTON_ABOUT,IDI_ICON_ABOUT,_T("About"));
    pToolbar->AddSeparator();
    pToolbar->AddButton(IDC_BUTTON_DONATION,IDI_ICON_DONATION,_T("Make Donation"));

    HWND hWndCombo = ::GetDlgItem(mhWndDialog,IDC_COMBO_FILES_TYPE);
    ComboBox_AddString(hWndCombo,_T("*.dll"));
    ComboBox_AddString(hWndCombo,_T("*.dll;*.exe"));
    ComboBox_AddString(hWndCombo,_T("*.dll;*.ocx;*.exe"));
    ComboBox_AddString(hWndCombo,_T("*.dll;*.ocx;*.exe;*.sys;*.cpl;*.scr"));
    ComboBox_SetCurSel(hWndCombo,0);
    ::PostMessage(hWndCombo,CB_SETEDITSEL,0,0);

    pStatusbar = new CStatusbar(mhInstance,mhWndDialog);
    pStatusbar->SetText(0,_T(""));

    // render layout
    Resize();
}

//-----------------------------------------------------------------------------
// Name: Close
// Object: Close 
// Parameters :
//     in  : 
//     out :
//     return : 
//-----------------------------------------------------------------------------
void Close()
{
    // hide main window to avoid user interaction during closing
    ShowWindow(mhWndDialog,FALSE);

    // stop searching
    OnCancel();

    // delete pListview
    delete pListView;
    pListView=NULL;

    // delete Toolbar
    delete pToolbar;
    pToolbar=NULL;

    // delete 
    delete pStatusbar;
    pStatusbar=NULL;

    // end dialog
    EndDialog(mhWndDialog,0);
}

//-----------------------------------------------------------------------------
// Name: WndProc
// Object: Main dialog callback
// Parameters :
//     in  : 
//     out :
//     return : 
//-----------------------------------------------------------------------------
LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_INITDIALOG:
        mhWndDialog=hWnd;
        Init();
        break;
    case WM_CLOSE:
        Close();
        break;
    case WM_SIZING:
        CheckSize((RECT*)lParam);
        break;
    case WM_SIZE:
        if (pStatusbar)
            pStatusbar->OnSize(wParam,lParam);
        Resize();
        break;
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_BUTTON_START_STOP:
            OnStartStop();
            break;
        case IDC_BUTTON_SAVE:

            pToolbar->EnableButton(IDC_BUTTON_START_STOP,FALSE);
            pToolbar->EnableButton(IDC_BUTTON_SAVE,FALSE);

            if(pListView)
                pListView->Save();

            pToolbar->EnableButton(IDC_BUTTON_START_STOP,TRUE);
            pToolbar->EnableButton(IDC_BUTTON_SAVE,TRUE);

            break;
        case IDC_BUTTON_BROWSE_FOR_DIRECTORY:
            BrowseForDirectory(GetDlgItem(mhWndDialog,IDC_EDIT_PATH));
            break;
        case IDC_BUTTON_ABOUT:
            OnAbout();
            break;
        case IDC_BUTTON_HELP:
            OnHelp();
            break;
        case IDC_BUTTON_DONATION:
            OnDonation();
            break;
        }
        break;
    case WM_NOTIFY:
        if (pListView)
        {
            if (pListView->OnNotify(wParam,lParam))
                break;
        }
        if (pToolbar)
        {
            if (pToolbar->OnNotify(wParam,lParam))
                break;
        }
        if (pStatusbar)
        {
            if (pStatusbar->OnNotify(wParam,lParam))
                break;
        }
        break;
    default:
        return FALSE;
    }
    return TRUE;
}

//-----------------------------------------------------------------------------
// Name: CheckSize
// Object: called on WM_SIZING. Assume main dialog has a min with and hight
// Parameters :
//     in  : 
//     out :
//     In Out : RECT* pWinRect : window rect
//     return : 
//-----------------------------------------------------------------------------
void CheckSize(RECT* pWinRect)
{
    // check min width and min height
    if ((pWinRect->right-pWinRect->left)<MAIN_DIALOG_MIN_WIDTH)
        pWinRect->right=pWinRect->left+MAIN_DIALOG_MIN_WIDTH;
    if ((pWinRect->bottom-pWinRect->top)<MAIN_DIALOG_MIN_HEIGHT)
        pWinRect->bottom=pWinRect->top+MAIN_DIALOG_MIN_HEIGHT;
}

//-----------------------------------------------------------------------------
// Name: Resize
// Object: called on WM_SIZE. Resize all components
// Parameters :
//     return : 
//-----------------------------------------------------------------------------
void Resize()
{
    RECT Rect;
    RECT RectButtonBrowse;
    RECT RectListView;
    RECT RectDialog;
    RECT RectStatus;
    LONG SpaceBetweenControls;
    HWND hWnd;

    // resize toolbar
    pToolbar->Autosize();

    // get dialog rect
    ::GetClientRect(mhWndDialog,&RectDialog);

    CDialogHelper::GetClientWindowRect(mhWndDialog,pStatusbar->GetControlHandle(),&RectStatus);

    // resize ListView
    CDialogHelper::GetClientWindowRect(mhWndDialog,pListView->GetControlHandle(),&RectListView);
    SpaceBetweenControls=RectListView.left;
    SetWindowPos(pListView->GetControlHandle(),HWND_NOTOPMOST,0,0,
        RectDialog.right-RectDialog.left-2*SpaceBetweenControls,
        /*RectDialog.bottom*/RectStatus.top -RectListView.top-SpaceBetweenControls,
        SWP_NOACTIVATE|SWP_NOZORDER|SWP_NOREDRAW|SWP_NOMOVE);    


    // Search
    hWnd=GetDlgItem(mhWndDialog,IDC_EDIT_SEARCH);
    CDialogHelper::GetClientWindowRect(mhWndDialog,hWnd,&Rect);
    SetWindowPos(hWnd,HWND_NOTOPMOST,0,0,
        RectDialog.right-Rect.left-2*SpaceBetweenControls,
        Rect.bottom-Rect.top,// keep same height
        SWP_NOACTIVATE|SWP_NOZORDER|SWP_NOREDRAW|SWP_NOMOVE);   

    // browse button
    hWnd=GetDlgItem(mhWndDialog,IDC_BUTTON_BROWSE_FOR_DIRECTORY);
    CDialogHelper::GetClientWindowRect(mhWndDialog,hWnd,&RectButtonBrowse);
    SetWindowPos(hWnd,HWND_NOTOPMOST,
        RectDialog.right-(RectButtonBrowse.right-RectButtonBrowse.left)-2*SpaceBetweenControls,
        RectButtonBrowse.top,// keep same y position
        0,0,
        SWP_NOACTIVATE|SWP_NOZORDER|SWP_NOREDRAW|SWP_NOSIZE);  
    // update RectButtonBrowse infos
    CDialogHelper::GetClientWindowRect(mhWndDialog,hWnd,&RectButtonBrowse);

    // path
    hWnd=GetDlgItem(mhWndDialog,IDC_EDIT_PATH);
    CDialogHelper::GetClientWindowRect(mhWndDialog,hWnd,&Rect);
    SetWindowPos(hWnd,HWND_NOTOPMOST,0,0,
        RectButtonBrowse.left-Rect.left-2*SpaceBetweenControls,
        Rect.bottom-Rect.top,// keep same height
        SWP_NOACTIVATE|SWP_NOZORDER|SWP_NOREDRAW|SWP_NOMOVE);   


    // File Type
    hWnd=GetDlgItem(mhWndDialog,IDC_COMBO_FILES_TYPE);
    CDialogHelper::GetClientWindowRect(mhWndDialog,hWnd,&Rect);
    SetWindowPos(hWnd,HWND_NOTOPMOST,0,0,
        RectDialog.right-Rect.left-2*SpaceBetweenControls,
        Rect.bottom-Rect.top,// keep same height
        SWP_NOACTIVATE|SWP_NOZORDER|SWP_NOREDRAW|SWP_NOMOVE); 

    CDialogHelper::Redraw(mhWndDialog);
}


//-----------------------------------------------------------------------------
// Name: CallBackPopUpMenuItem
// Object: Called each time a list view pop up menu item is click (for no CListview internal menu only)
// Parameters :
//     in  : UINT MenuID : Id of menu
//     out :
//     return : 
//-----------------------------------------------------------------------------
void CallBackPopUpMenuItem(UINT MenuID,LPVOID UserParam)
{
    UNREFERENCED_PARAMETER(UserParam);
    int SelectedItem=pListView->GetSelectedIndex();
    if (SelectedItem<0)
    {
        MessageBox(mhWndDialog,_T("No item selected"),_T("Error"),MB_OK | MB_ICONERROR | MB_TOPMOST);
        return;
    }

    TCHAR* psz;
    DWORD Size;
    if (MenuID==MenuCopyFunctionNameIndex)
    {
        int ColumnIndex;
        Size=pListView->GetItemTextLen(SelectedItem,ListviewSearchResultColumnsIndex_UndecoratedName);
        if (Size>1)
        {
            ColumnIndex=(int)ListviewSearchResultColumnsIndex_UndecoratedName;
        }
        else
        {
            Size=pListView->GetItemTextLen(SelectedItem,ListviewSearchResultColumnsIndex_FunctionName);
            ColumnIndex=(int)ListviewSearchResultColumnsIndex_FunctionName;
        }

        psz=(TCHAR*)_alloca(Size*sizeof(TCHAR));
        pListView->GetItemText(SelectedItem,ColumnIndex,psz,Size);
        CClipboard::CopyToClipboard(mhWndDialog,psz);
    }
    else if (MenuID==MenuCopyDllNameIndex)
    {
        Size=pListView->GetItemTextLen(SelectedItem,ListviewSearchResultColumnsIndex_FileName);
        psz=(TCHAR*)_alloca(Size*sizeof(TCHAR));
        pListView->GetItemText(SelectedItem,ListviewSearchResultColumnsIndex_FileName,psz,Size);
        CClipboard::CopyToClipboard(mhWndDialog,psz);
    }
    else if (MenuID==MenuCopyDllPathIndex)
    {
        Size=pListView->GetItemTextLen(SelectedItem,ListviewSearchResultColumnsIndex_FilePath);
        psz=(TCHAR*)_alloca(Size*sizeof(TCHAR));
        pListView->GetItemText(SelectedItem,ListviewSearchResultColumnsIndex_FilePath,psz,Size);
        CClipboard::CopyToClipboard(mhWndDialog,psz);
    }
}

//-----------------------------------------------------------------------------
// Name: CancelSearch
// Object: OnCancel heap entries walk (for thread cancellation)
// Parameters :
//     in  : 
//     out :
//     return : 
//-----------------------------------------------------------------------------
DWORD WINAPI CancelSearch(LPVOID lpParameter)
{
    UNREFERENCED_PARAMETER(lpParameter);
    OnCancel();
    return 0;
}

//-----------------------------------------------------------------------------
// Name: OnStartStop
// Object: OnStart or stop heap entries walking
// Parameters :
//     in  : 
//     out :
//     return : 
//-----------------------------------------------------------------------------
void OnStartStop()
{
    // if a heap is being parsed, 
    if (IsSearching())
    {
        // stop parsing
        pToolbar->EnableButton(IDC_BUTTON_START_STOP,FALSE);
        CloseHandle(CreateThread(NULL,0,CancelSearch,0,0,NULL));
    }   
    else
    {
        // start parsing
        pToolbar->EnableButton(IDC_BUTTON_START_STOP,FALSE);
        OnStart();
    }
        
}

//-----------------------------------------------------------------------------
// Name: OnCancel
// Object: OnCancel heap entries walk (blocking)
// Parameters :
//     in  : 
//     out :
//     return : 
//-----------------------------------------------------------------------------
void OnCancel()
{
    if (IsSearching())
    {
        if (!hSearchingThread)
            return;

        // set cancel event
        SetEvent(hEvtCancel);

        // wait for end of searching thread
        WaitForSingleObject(hSearchingThread,TIMEOUT_CANCEL_SEARCH_THREAD);

        // close searching thread
        CloseHandle(hSearchingThread);
        hSearchingThread=NULL;

        SetToolbarMode(FALSE);

        pStatusbar->SetText(0,_T("Search Canceled"));
    }
}

//-----------------------------------------------------------------------------
// Name: IsSearching
// Object: allow to know if a heap is being parsed
// Parameters :
//     in  : 
//     out :
//     return : TRUE if a heap is being parsed
//-----------------------------------------------------------------------------
BOOL IsSearching()
{
    if (!hSearchingThread)
        return FALSE;

    // check the end of searching thread
    if (WaitForSingleObject(hSearchingThread,0)!=WAIT_OBJECT_0)
        return TRUE;

    // close searching thread
    CloseHandle(hSearchingThread);
    hSearchingThread=NULL;
    return FALSE;
}

//-----------------------------------------------------------------------------
// Name: OnStart
// Object: OnStart search
// Parameters :
//     in  : 
//     out :
//     return : 
//-----------------------------------------------------------------------------
void OnStart()
{
    // cancel previous walk if any
    OnCancel();

    // reset cancel event
    ResetEvent(hEvtCancel);

    // walk heap in a new thread
    hSearchingThread=CreateThread(NULL,0,FindExports,NULL,0,NULL);

    SetToolbarMode(TRUE);
}

//-----------------------------------------------------------------------------
// Name: CallBackFileFound
// Object: called for each dll in directory and sub directories
// Parameters :
//     in  : TCHAR* Directory : directory of search result
//           WIN32_FIND_DATA* pWin32FindData : search result
//           PVOID UserParam : user param, here the search pattern for functions
//     out :
//     return : 
//-----------------------------------------------------------------------------
BOOL CallBackFileFound(TCHAR* Directory,WIN32_FIND_DATA* pWin32FindData,PVOID UserParam)
{
    TCHAR* pszSearch=(TCHAR*)UserParam;
    TCHAR FileFullPath[MAX_PATH];

    if (CFileSearch::IsDirectory(pWin32FindData))// should not occurs as we search for "*.dll"
    {
        // continue parsing
        return TRUE;
    }

    // make full path
    _tcscpy(FileFullPath,Directory);
    _tcscat(FileFullPath,pWin32FindData->cFileName);

    TCHAR StatusText[2*MAX_PATH];
    _tcscpy(StatusText,_T("Parsing File "));
    _tcscat(StatusText,FileFullPath);
    pStatusbar->SetText(0,StatusText);

    // parse pseudo header
    CPE Pe;
    Pe.bDisplayErrorMessages=FALSE;
    if (!Pe.Parse(FileFullPath,TRUE,FALSE))
    {
// possible improvement : add warning/log

        // continue dll parsing
        return TRUE;
    }

    int ItemIndex;
    CLinkListItem* pItem;
    CPE::EXPORT_FUNCTION_ITEM* pExportFunction;
    CHAR* pcDecoratedFunctionName;
    TCHAR* pszUndecoratedFunctionName;
    CHAR UndecoratedFunctionName[2*MAX_PATH];
    BOOL bItemMatch;

    // for each exported function of the dll
    for (pItem=Pe.pExportTable->Head;pItem;pItem=pItem->NextItem)
    {
        bItemMatch=FALSE;

        pExportFunction=(CPE::EXPORT_FUNCTION_ITEM*)pItem->ItemData;

        // if ordinal export only
        if ( *pExportFunction->FunctionName==0 )
            continue;

        // try to undecorate function name
#if(defined(UNICODE)||defined(_UNICODE))
        CAnsiUnicodeConvert::UnicodeToAnsi(pExportFunction->FunctionName,&pcDecoratedFunctionName);
#else
        pcDecoratedFunctionName=pExportFunction->FunctionName;
#endif

        if (UnDecorateSymbolName(pcDecoratedFunctionName,UndecoratedFunctionName,2*MAX_PATH,UNDNAME_NAME_ONLY))
        {
#if(defined(UNICODE)||defined(_UNICODE))
            CAnsiUnicodeConvert::AnsiToUnicode(UndecoratedFunctionName,&pszUndecoratedFunctionName);
#else
            pszUndecoratedFunctionName=UndecoratedFunctionName;
#endif
        }
        else
        {
            pszUndecoratedFunctionName=NULL;
        }

#if(defined(UNICODE)||defined(_UNICODE))
        free(pcDecoratedFunctionName);
#endif

        // check if function match search
        if (pszUndecoratedFunctionName)
            bItemMatch=CWildCharCompare::WildICmp(pszSearch,pszUndecoratedFunctionName);
        else
            bItemMatch=CWildCharCompare::WildICmp(pszSearch,pExportFunction->FunctionName);

        // if function match
        if (bItemMatch)
        {
            // add decorated name
            ItemIndex=pListView->AddItem(pExportFunction->FunctionName);

            // add undecorated name if any
            if (pszUndecoratedFunctionName)
            {
                // add undecorated name only if different from decorated name
                if (_tcscmp(pExportFunction->FunctionName,pszUndecoratedFunctionName)!=0)
                    pListView->SetItemText(ItemIndex,ListviewSearchResultColumnsIndex_UndecoratedName,pszUndecoratedFunctionName);
            }

            // add dll name
            pListView->SetItemText(ItemIndex,ListviewSearchResultColumnsIndex_FileName,pWin32FindData->cFileName);

            // add dll path
            pListView->SetItemText(ItemIndex,ListviewSearchResultColumnsIndex_FilePath,FileFullPath);
        }
           
#if(defined(UNICODE)||defined(_UNICODE))
        if (pszUndecoratedFunctionName)
            free(pszUndecoratedFunctionName);
#endif

    }

    // continue parsing
    return TRUE;
}

//-----------------------------------------------------------------------------
// Name: FindExports
// Object: OnStart export searching
// Parameters :
//     in  : 
//     out :
//     return : 
//-----------------------------------------------------------------------------
DWORD WINAPI FindExports(LPVOID lpParameter)
{
    UNREFERENCED_PARAMETER(lpParameter);

    TCHAR Path[MAX_PATH];

    pListView->Clear();

    // get include subdirectories state
    BOOL IncludeSubdir=((pToolbar->GetButtonState(IDC_BUTTON_INCLUDE_SUBDIR) & TBSTATE_CHECKED) !=0);

    // get path
    GetDlgItemText(mhWndDialog,IDC_EDIT_PATH,Path,MAX_PATH);
    // assume path exists
    if (!CStdFileOperations::DoesDirectoryExists(Path))
    {
        MessageBox(mhWndDialog,_T("Please select a valid directory"),_T("Information"),MB_OK|MB_ICONINFORMATION|MB_TOPMOST);
        SetToolbarMode(FALSE);
        return 0;
    }

    // assume search path ends with '\'
    SIZE_T Size=_tcslen(Path);
    if (Path[Size-1]!='\\')
        _tcscat(Path,_T("\\"));

    // get search
    TCHAR* pszSearch;
    Size=SendMessage(GetDlgItem(mhWndDialog,IDC_EDIT_SEARCH),WM_GETTEXTLENGTH,0,0);
    // if a search pattern is specified
    if (Size>0)
    {
        pszSearch=(TCHAR*)_alloca((Size+1)*sizeof(TCHAR));
        GetDlgItemText(mhWndDialog,IDC_EDIT_SEARCH,pszSearch,Size+1);
        pszSearch[Size]=0;
    }
    else // else use a default search pattern
    {
        pszSearch=_T("*");
    }

    // search dll in directory
// _tcscat(Path,_T("*.dll"));
    TCHAR* pszExtensions;
    HWND HwndCombo;
    HwndCombo = ::GetDlgItem(mhWndDialog,IDC_COMBO_FILES_TYPE);
    Size = ComboBox_GetTextLength(HwndCombo);
    pszExtensions = (TCHAR*)_alloca((Size+1)*sizeof(TCHAR));
    ComboBox_GetText(HwndCombo, pszExtensions, Size+1);
        
    DWORD ArraySize=0;
    TCHAR** pExtensionArray = CMultipleElementsParsing::ParseString(pszExtensions,&ArraySize);

    BOOL bSearchCanceled;
    CFileSearch::SearchMultipleNames(Path,pExtensionArray,ArraySize,IncludeSubdir,hEvtCancel,CallBackFileFound,pszSearch,&bSearchCanceled);

    CMultipleElementsParsing::ParseStringArrayFree(pExtensionArray,ArraySize);

    // resort listview if needed
    pListView->ReSort();

    // warn the user the search is finished
    if(!bSearchCanceled)
        MessageBox(mhWndDialog,_T("Search Completed"),_T("Information"),MB_OK|MB_ICONINFORMATION|MB_TOPMOST);


    // at the end of search, restore toolbar buttons
    SetToolbarMode(FALSE);

    pStatusbar->SetText(0,_T(""));

    return 0;
}

//-----------------------------------------------------------------------------
// Name: SetToolbarMode
// Object: set toolbar mode (enable or disable some functionalities)
// Parameters :
//     in  : BOOL bIsWalking : TRUE if in walking mode
//     out :
//     return : 
//-----------------------------------------------------------------------------
void SetToolbarMode(BOOL bIsSearching)
{
    if (bIsSearching)
    {
        pToolbar->ReplaceIcon(IDC_BUTTON_START_STOP,CToolbar::ImageListTypeEnable,IDI_ICON_CANCEL);
        pToolbar->ReplaceIcon(IDC_BUTTON_START_STOP,CToolbar::ImageListTypeHot,IDI_ICON_CANCEL);
        pToolbar->ReplaceText(IDC_BUTTON_START_STOP,_T("Cancel"));
        pToolbar->EnableButton(IDC_BUTTON_START_STOP,TRUE);
        pToolbar->EnableButton(IDC_BUTTON_SAVE,FALSE);
        pToolbar->EnableButton(IDC_BUTTON_INCLUDE_SUBDIR,FALSE);
    }
    else
    {
        pToolbar->ReplaceIcon(IDC_BUTTON_START_STOP,CToolbar::ImageListTypeEnable,IDI_ICON_START);
        pToolbar->ReplaceIcon(IDC_BUTTON_START_STOP,CToolbar::ImageListTypeHot,IDI_ICON_START);
        pToolbar->ReplaceText(IDC_BUTTON_START_STOP,_T("Start"));
        pToolbar->EnableButton(IDC_BUTTON_START_STOP,TRUE);
        pToolbar->EnableButton(IDC_BUTTON_SAVE,TRUE);
        pToolbar->EnableButton(IDC_BUTTON_INCLUDE_SUBDIR,TRUE);
    }
}

//-----------------------------------------------------------------------------
// Name: OnAbout
// Object: show about dlg box
// Parameters :
//     in  : 
//     out :
//     return : 
//-----------------------------------------------------------------------------
void OnAbout()
{
    ShowAboutDialog(mhInstance,mhWndDialog);
}
//-----------------------------------------------------------------------------
// Name: OnHelp
// Object: show help file
// Parameters :
//     in  : 
//     out :
//     return : 
//-----------------------------------------------------------------------------
void OnHelp()
{
    
    TCHAR sz[MAX_PATH];
    CStdFileOperations::GetAppPath(sz,MAX_PATH);
    _tcscat(sz,HELP_FILE);
    if (((int)ShellExecute(NULL,_T("open"),sz,NULL,NULL,SW_SHOWNORMAL))<33)
    {
        _tcscpy(sz,_T("Error opening help file "));
        _tcscat(sz,HELP_FILE);
        MessageBox(mhWndDialog,sz,_T("Error"),MB_OK|MB_ICONERROR|MB_TOPMOST);
    }
}

//-----------------------------------------------------------------------------
// Name: OnDonation
// Object: query to make donation
// Parameters :
//     in  : 
//     out :
//     return : 
//-----------------------------------------------------------------------------
void OnDonation()
{
    BOOL bEuroCurrency=FALSE;
    HKEY hKey;
    wchar_t pszCurrency[2];
    DWORD Size=2*sizeof(wchar_t);
    memset(pszCurrency,0,Size);
    TCHAR pszMsg[3*MAX_PATH];

    // check that HKEY_CURRENT_USER\Control Panel\International\sCurrency contains the euro symbol
    // retrieve it in unicode to be quite sure of the money symbol
    if (RegOpenKeyEx(HKEY_CURRENT_USER,_T("Control Panel\\International"),0,KEY_QUERY_VALUE,&hKey)==ERROR_SUCCESS)
    {
        // use unicode version only to make string compare
        if (RegQueryValueExW(hKey,L"sCurrency",NULL,NULL,(LPBYTE)pszCurrency,&Size)==ERROR_SUCCESS)
        {
            if (wcscmp(pszCurrency,L"€")==0)
                bEuroCurrency=TRUE;
        }
        // close open key
        RegCloseKey(hKey);
    }
    // yes, you can do it if u don't like freeware and open sources soft
    // but if you make it, don't blame me for not releasing sources anymore
    _tcscpy(pszMsg,
                    _T("https://www.paypal.com/cgi-bin/webscr?cmd=_xclick&business=jacquelin.potier@free.fr")
                    _T("&currency_code=USD&lc=EN&country=US")
                    _T("&item_name=Donation%20for%20DllExportFinder&return=http://jacquelin.potier.free.fr/DllExportFinder/"));
    // in case of euro monetary symbol
    if (bEuroCurrency)
        // add it to link
        _tcscat(pszMsg,_T("&currency_code=EUR"));

    // open donation web page
    if (((int)ShellExecute(NULL,_T("open"),pszMsg,NULL,NULL,SW_SHOWNORMAL))<33)
        // display error msg in case of failure
        MessageBox(mhWndDialog,_T("Error Opening default browser. You can make a donation going to ")
        _T("http://jacquelin.potier.free.fr"),
        _T("Error"),MB_OK|MB_ICONERROR|MB_TOPMOST);
}