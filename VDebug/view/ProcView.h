#ifndef PROCVIEW_VDEBUG_H_H_
#define PROCVIEW_VDEBUG_H_H_
#include <Windows.h>
#include <CommCtrl.h>
#include <set>
#include <vector>
#include <ComLib/ComLib.h>
#include <ComLib/ComLib.h>
#include "ViewBase.h"

using namespace std;

struct ProcShowInfo
{
    ProcMonInfo info;       //proc info
    DWORD m_dwIcoIdex;      //ico index
    std::mstring m_indexStr;//index str
    std::mstring m_procShow;//proc show

    ProcShowInfo() {
        m_dwIcoIdex = -1;
    }
};

typedef LRESULT (CALLBACK *PWIN_PROC)(HWND, UINT, WPARAM, LPARAM);

class CProcSelectView : public CWindowBase, public CCriticalSectionLockable
{
public:
    CProcSelectView() : m_hParent(NULL), m_hFont(NULL), m_hProcList(NULL), m_hImageList(NULL), m_dwSelectPid(0)
    {
        m_pfnFilterEditProc = NULL;
    }

    virtual ~CProcSelectView()
    {}

    BOOL SetFont(HFONT hFont);

    BOOL Refush();

    BOOL Create();

    BOOL DoModule(HWND hParent);

    DWORD GetSelectProc()
    {
        return m_dwSelectPid;
    }

    void OnProcChanged(const ProcInfoSet &info);
    void DeleteFromSet(vector<ProcShowInfo *> &procSet, const list<DWORD> &killed, bool freeMem);
    void DeleteProcCache();
protected:
    INT_PTR OnInitDlg(HWND hwnd, WPARAM wp, LPARAM lp);

    VOID OnGetListCtrlDisplsy(IN OUT NMLVDISPINFOA* ptr);
    void OnListColumnClick(IN NMLISTVIEW *ptr);

    void OnListItemChanged(HWND hwnd, WPARAM wp, LPARAM lp);
    INT_PTR OnNotify(HWND hwnd, WPARAM wp, LPARAM lp);
    INT_PTR OnClose(HWND hwnd, WPARAM wp, LPARAM lp);
    INT_PTR OnCommand(HWND hwnd, WPARAM wp, LPARAM lp);
    INT_PTR OnFilterProc(HWND hwnd, WPARAM wp, LPARAM lp);
    static LRESULT WINAPI FilterEditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

protected:
    void InitListCtrl();
    VOID CalWidthByColumns() const;
    void RefushProc();
    int GetFileIco(const mstring &wstrFile);
    mstring GetSearchStr(ProcShowInfo *ptr);
    mstring GetLastSelectDir();
    void RecordLastSelect(LPCWSTR wszDir);

protected:
    virtual LRESULT OnWindowMsg(HWND hwnd, UINT uMsg, WPARAM wp, LPARAM lp);

protected:
    HWND m_hParent;
    HWND m_hEditFlt;
    HWND m_hEditInfo;
    HWND m_hEditStatus;
    HWND m_hProcList;
    HFONT m_hFont;
    DWORD m_dwSelectPid;
    PWIN_PROC m_pfnFilterEditProc;

    HIMAGELIST m_hImageList;
    map<mstring, int> m_icoIndex;
    vector<ProcShowInfo *> m_procShow;
    vector<ProcShowInfo *> m_procAll;
    mstring m_searchStr;
    mstring m_statusMsg;

    static map<mstring, HICON> ms_peIcon;
};
#endif