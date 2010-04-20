// PlayerMgr.cpp : 实现文件
//

#include "stdafx.h"
//#include "emule.h"
#include "PlayerMgr.h"
#include ".\playermgr.h"

#define _MAX_BUFFER_SIZE (1024*1024*10)
const int _REQ_DATA_LEN =(1024*1024);
// CPlayerMgr

const char * httphead="HTTP/1.1 200 OK\r\nServer: Microsoft-IIS/5.0\r\nX-Powered-By: ASP.NET\r\nContent-Length: %d\r\n\r\n";
//const char * httphead_part = "HTTP/1.1 206 Partial content\r\nServer: Microsoft-IIS/5.0\r\nX-Powered-By: ASP.NET\r\nContent-Length: %d\r\nContent-Range: bytes %d-%d/%d\r\n\r\n";
//const char * httphead_part = "HTTP/1.1 206 Partial content\r\nServer: Microsoft-IIS/5.0\r\nX-Powered-By: ASP.NET\r\nContent-Range: bytes %d-\r\n\r\n";
//const char * httphead_part = "HTTP/1.1 206 Partial content\r\nContent-Length: %d\r\nContent-Range: bytes %d-%d/%d\r\n\r\n";
const char * httphead_part = "HTTP/1.1 206 Partial content\r\nContent-Range: bytes %d-\r\n\r\n";

inline bool IsSocketClosed(DWORD dwError)
{
	switch(dwError)
	{
	case WSAECONNABORTED:
	case WSAENOTSOCK:
	case WSAECONNRESET:
	case WSAESHUTDOWN:
		return true;
	}

	return false;
}

CMutex CPlayerMgr::m_Mutex;
CPlayerMgr * g_PlayerMgr = NULL;
//PlayerDataCallback CPlayerMgr::m_PlayerCallback = NULL;
HWND g_hNotifyWnd = NULL;

IMPLEMENT_DYNCREATE(CPlayerMgr, CWinThread)

CPlayerMgr::CPlayerMgr()
{
	m_nListenPort = 0;
}

void CListenPlayerSocket::OnAccept(int nErrorCode)
{
	// TODO: 在此添加专用代码和/或调用基类
	CAsyncSocket * pNewSock = new CPlayerTaskSocket;
	if(!Accept(*pNewSock))
	{
		delete pNewSock;
	}
	else
	{
		TRACE("%08x new connection\n", pNewSock);
		pNewSock->AsyncSelect(FD_READ);
		
	}
	CAsyncSocket::OnAccept(nErrorCode);
}

bool CPlayerTask::RequestData()
{
	if(m_SockList.IsEmpty())
		return false;

	POSITION pos = m_SockList.GetHeadPosition();
	while(pos)
	{
		POSITION posOld = pos;
		CPlayerTaskSocket * sk = m_SockList.GetNext(pos);
		if(! sk->RequestData())
		{
			m_SockList.RemoveAt(posOld);
			g_PlayerMgr->AddSocketToDelete(sk);
		}
	}
	return true;
}

bool CPlayerTaskSocket::RequestData()
{
	DWORD tmNow = GetTickCount();

	if(m_tmSendData && (tmNow - m_tmSendData > 1000 * 10))
	{
		if(m_dwCurrentPos>=m_pTask->m_dwTotalFileSize)
		{
			if(m_BufferLst.IsEmpty())
			{
				//return false;
			}
			else SendBuffer(false);
		}
		else SendTinyData();
	}

	if(tmNow < m_tmReqData)
		return true;

	ASSERT(m_pTask);
	m_tmReqData +=100;

	if(m_dwCurrentPos>=m_pTask->m_dwTotalFileSize)
	{
		SendBuffer(false);
		if(m_BufferLst.IsEmpty())
		{
			return false;
		}
		return true;
	}

	if(m_nTotalBufferSize>_MAX_BUFFER_SIZE)
	{
		SendBuffer(true);
		m_tmReqData += 1000;
		return true;
	}

	PLAYER_DATA_REQ * req=new PLAYER_DATA_REQ;
	memcpy(req->filehash, m_pTask->m_filehash, 16);
	req->pos = m_dwCurrentPos;
	req->len = _REQ_DATA_LEN;
	TRACE("%08x current pos=%d\n", this, m_dwCurrentPos);
	if(req->pos + req->len > m_pTask->m_dwTotalFileSize)
	{
		req->len = m_pTask->m_dwTotalFileSize - req->pos;
	}

	ASSERT(g_hNotifyWnd);
	::PostMessage(g_hNotifyWnd, WM_PLAYER_DATA_REQ, 0, (LPARAM) req);

	return true;
}

void CPlayerTaskSocket::SaveBuffer(void * pData, int nLen)
{
	if(pData==NULL || nLen<=0)
		return;

	TCP_BUFFER * pBuf = new TCP_BUFFER;
	pBuf->nType = _TCP_BUF_RAW;
	pBuf->nLen = nLen;
	pBuf->nOffset = 0;
	pBuf->pBuf = new char [nLen];
	memcpy(pBuf->pBuf, pData, nLen);

	m_BufferLst.AddTail( pBuf);
	m_nTotalBufferSize += nLen;
}

void CPlayerTaskSocket::SendBuffer(bool bKeepTail)
{
	int nRet;
	while(! m_BufferLst.IsEmpty())
	{
		if(bKeepTail && (m_nTotalBufferSize<MAX_CACHE_SIZE || m_BufferLst.GetSize()==1))
			break;

		TCP_BUFFER * pBuf = m_BufferLst.GetHead();
		if(pBuf->nType==_TCP_BUF_RAW)
		{
			nRet = Send(pBuf->pBuf + pBuf->nOffset, pBuf->nLen-pBuf->nOffset);
			if(nRet<=0)
			{
				m_tmReqData += 1000*10;

				DWORD dwErr=WSAGetLastError();
				if(IsSocketClosed(dwErr))
				{
					if(m_pTask)
					{
						POSITION pos = m_pTask->m_SockList.Find(this);
						if(pos) m_pTask->m_SockList.RemoveAt(pos);
						g_PlayerMgr->AddSocketToDelete(this);
					}
				}
				break;
			}

			m_tmSendData = GetTickCount();
			m_nTotalBufferSize -= nRet;
			if(nRet==pBuf->nLen - pBuf->nOffset)
			{
				delete [] pBuf->pBuf;
				delete pBuf;

				m_BufferLst.RemoveHead();
			}
			else
			{
				pBuf->nOffset += nRet;
				break;
			}
		}
		//else if(pBuf->nType==_TCP_BUF_FILE)
		//{
		//	if(pBuf->nLen<=0)
		//	{
		//		delete pBuf;
		//		m_BufferLst.RemoveHead();
		//		continue;
		//	}

		//	m_fileToRead.Seek(pBuf->nOffset, CFile::begin);
		//	char buf[10240];
		//	int to_read = pBuf->nLen>10240? 10240 : pBuf->nLen;
		//	int ret_read = m_fileToRead.Read(buf, to_read);
		//	nRet = m_pSocket->Send(buf, ret_read);
		//	if(nRet<=0)
		//		break;
		//	else
		//	{
		//		pBuf->nOffset+=nRet;
		//		pBuf->nLen -= nRet;

		//		if(nRet<ret_read)
		//			break;
		//	}

		//	m_dwTotalSent += nRet;
		//}
	}
}

void CPlayerTaskSocket::SendTinyData()
{
	int nRet;
	if(! m_BufferLst.IsEmpty())
	{
		TCP_BUFFER * pBuf = m_BufferLst.GetHead();

		if(pBuf->nOffset==pBuf->nLen)
		{
			delete [] pBuf->pBuf;
			delete pBuf;

			m_BufferLst.RemoveHead();
			SendTinyData();
			return;
		}

		ASSERT(pBuf->nOffset<pBuf->nLen);

		nRet = Send(pBuf->pBuf + pBuf->nOffset, 1);
		if(nRet<=0) return;

		m_tmSendData = GetTickCount();
		m_nTotalBufferSize -= nRet;
		if(nRet==pBuf->nLen - pBuf->nOffset)
		{
			delete [] pBuf->pBuf;
			delete pBuf;

			m_BufferLst.RemoveHead();
		}
		else
		{
			pBuf->nOffset += nRet;
		}
	}
}

//bool CPlayerTask::SendData(void * pData, int nLen)
//{
//	if(! m_pSocket)
//	{
//		if(m_nTotalBufferSize>_MAX_BUFFER_SIZE)
//			return false;
//
//		SaveBuffer(pData, nLen);
//		return true;
//	}
//
//	SendBuffer();
//
//	if(! m_BufferLst.IsEmpty())
//	{
//		if(m_nTotalBufferSize>_MAX_BUFFER_SIZE)
//			return false;
//
//		SaveBuffer(pData, nLen);
//		return true;
//	}
//
//	int nRet=m_pSocket->Send(pData, nLen);
//	if(nRet<=0)
//	{
//		if(m_nTotalBufferSize>_MAX_BUFFER_SIZE)
//			return false;
//
//		SaveBuffer(pData, nLen);
//	}
//	else if(nRet<nLen)
//	{
//		const char * p=(char*)pData;
//
//		SaveBuffer((void*)(p+nRet), nLen-nRet);
//	}
//	return true;
//}

CPlayerTaskSocket::~CPlayerTaskSocket()
{
	if(g_PlayerMgr)
	{
		g_PlayerMgr->RemovePlayerTaskBySocket(this);
	}

	POSITION pos = m_BufferLst.GetHeadPosition();
	while(pos)
	{
		TCP_BUFFER * buf=m_BufferLst.GetNext(pos);
		delete []buf->pBuf;
		delete buf;
	}
	m_BufferLst.RemoveAll();
}

void CPlayerTaskSocket::ClearBuffer()
{
	POSITION pos = m_BufferLst.GetHeadPosition();
	while(pos)
	{
		TCP_BUFFER * pBuf= m_BufferLst.GetNext(pos);

		delete [] pBuf->pBuf;
		delete pBuf;
	}
}
void CPlayerTask::AddSocket(CPlayerTaskSocket * sk)
{
	m_bStarted = true;

	if(m_SockList.Find(sk, NULL))
		return;

	m_SockList.AddTail(sk);
	sk->m_pTask = this;
}

void CPlayerTaskSocket::OnReceive(int nErrorCode)
{
	ASSERT(g_PlayerMgr);

	char buffer[10240];
	int n=Receive(buffer, 10240);
	if(n>0)
	{
		buffer[n] = 0;
		CStringA strHashId;
		int nRange = 0;
		if(ParseHttpReq(buffer, strHashId, nRange))
		{
			CSKey key;
			DWORD * pKey = (DWORD *)key.m_key;
			sscanf(strHashId, "%08x%08x%08x%08x", pKey, pKey+1, pKey+2, pKey+3);

			CSingleLock slock(&g_PlayerMgr->m_Mutex, true);

			CPlayerTask * pTask = g_PlayerMgr->GetPlayerTask(key);
			if(pTask)
			{
				TRACE("file size =%d\n", pTask->m_dwTotalFileSize);
				//m_bRangeReq = true;
				ASSERT(m_nTotalBufferSize==0);
				pTask->AddSocket(this);
				//if(nRange > 222406523)
				//	nRange = 222406523;
				m_dwCurrentPos = nRange;
				m_bSentData = true;
				if(nRange)
				{
					//if(m_dwTailPos==0)
					{
						//m_dwTailPos = m_dwCurrentPos;
						//CStringA strHead = pTask->GetHeader(m_dwTailPos);
						//SaveBuffer((void*)(const char*)strHead, strHead.GetLength());
					}
					//else
					{
					}
				}
				//else
				{
					CStringA strHead = pTask->GetHeader(nRange);
					SaveBuffer((void*)(const char*)strHead, strHead.GetLength());
				}
			}

		}
	}

	CAsyncSocket::OnReceive(nErrorCode);
}

bool CPlayerTaskSocket::ParseHttpReq(CStringA strHtml, CStringA & rHashId, int & rnRange)
{
	rnRange = 0;
	rHashId.Empty();

	//  Not http GET command
	if(strnicmp(strHtml, "GET ", 4)!=0)
		return false;

	strHtml.Delete(0, 4);
	if(strHtml.GetAt(0)=='/')
		strHtml.Delete(0);

	int i;
	i=strHtml.Find("/");
	if(i<0) return false;

	rHashId = strHtml.Mid(0, i);
	strHtml.MakeLower();
	int n=strHtml.Find("range:");
	if(n>0)
	{
		n+=6;
		int iRangeEnd=strHtml.Find("\r\n", n);
		CStringA strRange=strHtml.Mid(n, iRangeEnd-n);
		while(! strRange.IsEmpty())
		{
			if(!isdigit(strRange.GetAt(0)))
				strRange.Delete(0);
			else break;
		}

		if(strRange.GetAt(strRange.GetLength()-1)=='-')
			strRange.Delete(strRange.GetLength()-1);

		strRange.Trim();

		rnRange = atol(strRange);
	}
	return true;
}
bool g_bWaitExit=false;
CPlayerMgr::~CPlayerMgr()
{
	g_bWaitExit = true;
}

BOOL CPlayerMgr::InitInstance()
{
	return TRUE;
}

int CPlayerMgr::ExitInstance()
{
	// TODO: 在此执行任意逐线程清理
	CSingleLock slock(&m_Mutex, true);

	POSITION pos = m_TaskLst.GetStartPosition();
	while(pos)
	{
		CSKey key;
		CPlayerTask * p;
		m_TaskLst.GetNextAssoc(pos, key, p);
		if(p) delete p;
	}
	m_TaskLst.RemoveAll();

	return CWinThread::ExitInstance();
}

BEGIN_MESSAGE_MAP(CPlayerMgr, CWinThread)
END_MESSAGE_MAP()

// CPlayerMgr 消息处理程序

CStringA CPlayerTask::GetHeader(DWORD dwPos)
{
	CStringA strTmpA;
	if(dwPos==0)
		strTmpA.Format(httphead, m_dwTotalFileSize);
	else
	{
		DWORD content_len = m_dwTotalFileSize - dwPos;
		//DWORD stop_pos = m_dwTotalFileSize - 1;
		DWORD stop_pos;// = dwPos + 1024*1024*5;
		stop_pos = m_dwTotalFileSize-1;
		content_len = stop_pos - dwPos;
		//strTmpA.Format(httphead_part, content_len, dwPos, stop_pos, m_dwTotalFileSize);
		strTmpA.Format(httphead_part, dwPos);
	}
	return strTmpA;
}

int StartPlayer(const uchar * filehash, CString strFilename, DWORD dwFileSize, CString strExt)
{
	if(! g_PlayerMgr)
	{
		g_hNotifyWnd = AfxGetMainWnd()->GetSafeHwnd();
		g_PlayerMgr = (CPlayerMgr*)AfxBeginThread(RUNTIME_CLASS(CPlayerMgr));

		//  开始成功端口侦听
		while(g_PlayerMgr->GetListenPort()==0)
		{
			Sleep(100);
		}
	}

	if(strExt.IsEmpty() || filehash==NULL)
		return 0;

	CSKey key(filehash);

	CSingleLock lock(&CPlayerMgr::m_Mutex, true);

	TCHAR szBuf[_MAX_PATH];
	ULONG ulSize = _MAX_PATH;

	CString strExeFile;
	CRegKey reg;
	if(reg.Open(HKEY_CLASSES_ROOT, _T(".")+strExt)==ERROR_SUCCESS)
	{
		if(reg.QueryStringValue(NULL, szBuf, &ulSize)==ERROR_SUCCESS)
		{
			CString strCmd;
			strCmd.Format(_T("%s\\shell\\open\\command"), szBuf);
			if(reg.Open(HKEY_CLASSES_ROOT, strCmd)==ERROR_SUCCESS)
			{
				ulSize = _MAX_PATH;
				if(reg.QueryStringValue(NULL, szBuf, &ulSize)==ERROR_SUCCESS)
				{
					strExeFile = szBuf;
				}
			}
		}
	}

	//  使用配置的播放器
	if(strExeFile.IsEmpty())
	{
		// todo
		return 4;
	}

	CString strCmdLine;
	DWORD * pKey=(DWORD*)key.m_key;
	strCmdLine.Format(_T("http://127.0.0.1:%d/%08x%08x%08x%08x/%s"), g_PlayerMgr->GetListenPort(),
		pKey[0], pKey[1], pKey[2], pKey[3], strFilename);

	strExeFile.Replace(_T("\"%1\""), _T(""));
	strExeFile.Replace(_T("%1"), _T(""));
	strExeFile.Trim();

	//  把参数剥出来
	int nParamPos = -1;
	CString strTmp = strExeFile;
	while(true)
	{
		nParamPos=strTmp.ReverseFind(_T('.'));
		if(nParamPos==-1)
			break;

		LPCTSTR szExe = strTmp;
		if(_tcsnicmp(szExe + nParamPos, _T(".exe"), 4)==0)
		{
			nParamPos += 4;
			break;
		}
		else
		{
			strTmp = strTmp.Left(nParamPos);
		}
	}

	if(nParamPos==-1)
	{
		return 4;
	}

	CString strExeParam;
	if(nParamPos<strExeFile.GetLength()-1)
	{
		if(strExeFile.GetAt(nParamPos)==_T('\"'))
			nParamPos++;

		strExeParam = strExeFile.Mid(nParamPos);
		strExeFile = strExeFile.Left(nParamPos);
	}

	CPlayerTask * pTask = g_PlayerMgr->AddPlayerTask(key);
	pTask->m_dwTotalFileSize = dwFileSize;
//	CStringA strTmpA;
//	strTmpA.Format(httphead, dwFileSize);
//	pTask->SendData((void*)(const char*)strTmpA, strTmpA.GetLength());

	//if(! pTask->m_fileToRead.Duplicate() Open(strFilepath, CFile::modeRead|CFile::shareDenyWrite))
	//{
	//	g_PlayerMgr->RemovePlayerTask(key);
	//	return 3;
	//}

	ShellExecute(NULL,_T("open"), strExeFile, strCmdLine + strExeParam, NULL, SW_SHOW);
	return 0;
}

int CPlayerMgr::Run()
{
	// TODO: 在此添加专用代码和/或调用基类
	ASSERT_VALID(this);
#ifndef _AFXDLL 
#define _AFX_SOCK_THREAD_STATE AFX_MODULE_THREAD_STATE 
#define _afxSockThreadState AfxGetModuleThreadState() 
	_AFX_SOCK_THREAD_STATE* pState = _afxSockThreadState; 
	if (pState->m_pmapSocketHandle == NULL) 
		pState->m_pmapSocketHandle = new CMapPtrToPtr; 
	if (pState->m_pmapDeadSockets == NULL) 
		pState->m_pmapDeadSockets = new CMapPtrToPtr; 
	if (pState->m_plistSocketNotifications == NULL) 
		pState->m_plistSocketNotifications = new CPtrList; 
#endif

	if (!m_skListen.Socket(SOCK_STREAM, FD_ACCEPT))
		return false;

	srand(time(NULL));
	WORD nListenPort;
	while(true)
	{
#ifdef _DEBUG
		nListenPort = 8880;
#else
		nListenPort = 3000 + rand() % 1200;
#endif
		if(m_skListen.Bind(nListenPort))
			break;
		else
		{
			TRACE("bind error: %d\n", WSAGetLastError());
		}
	}

	m_nListenPort = nListenPort;

	m_skListen.Listen(5);

	_AFX_THREAD_STATE* pTState = AfxGetThreadState();

	while(true)
	{
		Process();
		if(::PeekMessage(&(pTState->m_msgCur), NULL, NULL, NULL, PM_NOREMOVE))
		{
			if (!PumpMessage())
				return ExitInstance();
		}
	}

	return 0;
}

bool CPlayerTask::SendData(DWORD dwPos, void * pData, int nLen)
{
	if(m_SockList.IsEmpty())
		return false;

	bool bSent = false;
	POSITION pos = m_SockList.GetHeadPosition();
	while(pos)
	{
		POSITION posOld = pos;
		CPlayerTaskSocket * sk=m_SockList.GetNext(pos);
		if(sk->m_dwCurrentPos == dwPos)
		{
			//  数据太少，保存下来，用来保持连接
			if(nLen<=MAX_CACHE_SIZE)
			{
				if(nLen<=0 || pData==NULL)
				{
					sk->m_tmReqData += 1000*10;
					continue;
				}

				//if(sk->m_nTotalBufferSize>_MAX_BUFFER_SIZE)
				//	continue;

				if(sk->m_nTotalBufferSize > MAX_CACHE_SIZE && nLen>=MAX_CACHE_SIZE)
					sk->SendBuffer(false);

				sk->SaveBuffer(pData, nLen);
				sk->m_dwCurrentPos+=nLen;

				if(sk->m_dwCurrentPos>=m_dwTotalFileSize)
				{
					sk->SendBuffer(false);
				}

				continue;
			}
			else
			{
				//  先把老的数据发送
				if(!sk->m_BufferLst.IsEmpty())
				{
					sk->SendBuffer(false);
				}

				if(!sk->m_BufferLst.IsEmpty())
				{
					sk->SaveBuffer(pData, nLen);
					sk->m_dwCurrentPos += nLen;
					sk->m_tmReqData += 1000 * 10;
					continue;
				}
			}

			bSent = true;
			int nSent=-1;
			//if(m_dwTotalFileSize - dwPos > 1024*1024*4)
			//	nSent=sk->Send(pData, nLen-MAX_CACHE_SIZE);
			//else nSent = sk->Send(pData, nLen);
			if(m_dwTotalFileSize -1 <= dwPos + nLen)
				nSent = sk->Send(pData, nLen);
			else nSent=sk->Send(pData, nLen-MAX_CACHE_SIZE);
			ASSERT(nLen>MAX_CACHE_SIZE);
			//nSent=sk->Send(pData, nLen-MAX_CACHE_SIZE);

			if(nSent<0)
			{
				DWORD dwErr=WSAGetLastError();

				if(IsSocketClosed(dwErr))
				{
					m_SockList.RemoveAt(posOld);
					g_PlayerMgr->AddSocketToDelete(sk);
				}
				else
				{
					sk->SaveBuffer((char*)pData, nLen);
					sk->m_dwCurrentPos += nLen;
				}

				continue;
			}

			sk->m_tmSendData = GetTickCount();

			if(nSent<nLen-MAX_CACHE_SIZE)
			{
				sk->m_tmReqData += 1000 * 2;
			}

			if(nLen<_REQ_DATA_LEN)
			{
				sk->m_tmReqData += 1000 * 10;
			}

			sk->m_dwCurrentPos += nLen;
			//  保存后面的一小部分，用来保持连接
			sk->SaveBuffer((char*)pData+nSent, nLen - nSent);

			//  到文件尾部，全部发送掉
			if(sk->m_dwCurrentPos>=m_dwTotalFileSize)
			{
				sk->SendBuffer(false);
			}
		}
	}

	return bSent;
}

void CPlayerMgr::Process()
{
	if(! m_ForDelete.IsEmpty())
	{
		CSingleLock slock(&m_Mutex, true);
		POSITION pos = m_ForDelete.GetStartPosition();
		while(pos)
		{
			CPlayerTaskSocket * sk;
			m_ForDelete.GetNextAssoc(pos, sk, sk);
			TRACE("%08x delete socket\n", sk);
			delete sk;
		}

		m_ForDelete.RemoveAll();
	}

	if(m_TaskLst.IsEmpty())
		Sleep(1000);
	else
	{
		Sleep(80);

		CSingleLock slock(&m_Mutex, true);

		POSITION pos = m_TaskLst.GetStartPosition();

		// VC-linhai[2007-08-06]:warning C4189: “dwNow” : 局部变量已初始化但不引用
		//DWORD dwNow = GetTickCount();
		while(pos)
		{
			CPlayerTask * task=NULL;
			CSKey k;
			m_TaskLst.GetNextAssoc(pos, k, task);
			if(task && task->m_SockList.IsEmpty()==FALSE)
			{
				if(! task->RequestData())
				{
					m_TaskLst.RemoveKey(k);
					pos = m_TaskLst.GetStartPosition();
				}
			}

			if(task && task->m_SockList.IsEmpty() && task->m_bStarted)
			{
				if(task->m_tmEmptyTask==0)
					task->m_tmEmptyTask = time(NULL);
				else if(time(NULL) - task->m_tmEmptyTask > 12)
				{
					m_TaskLst.RemoveKey(k);
					delete task;
				}
			}
			else
			{
				if(task) task->m_tmEmptyTask = 0;
			}
		}
	}
}

int PlayerDataArrived(PLAYER_DATA_REQ * req, void * data, int len)
{
	if(req==NULL)
	{
		return 2;
	}
	if(!g_PlayerMgr) return 3;

	CSingleLock slock(&CPlayerMgr::m_Mutex, true);

	DWORD pos = req->pos;
	CSKey key(req->filehash);
	delete req;

	CPlayerTask * pTask = g_PlayerMgr->GetPlayerTask(key);
	if(! pTask)
		return 3;

	if(!pTask->SendData(pos, data, len))
	{
		return 4;
	}



	return 0;
}

//void SetPlayerDataCallback(PlayerDataCallback pcb)
//{
//	CPlayerMgr::m_PlayerCallback = pcb;
//}
//
//void GetPlayerDataFromFile(const uchar * filehash, DWORD dwPos, DWORD dwLen)
//{
//	CSingleLock slock(&CPlayerMgr::m_Mutex, true);
//
//	CSKey key(filehash);
//	CPlayerTask * pTask = g_PlayerMgr->GetPlayerTask(key);
//	if(! pTask) return;
//
//	pTask->SaveBuffer(dwPos, dwLen);
//}

bool IsTaskExist(const uchar * filehash)
{
	if(g_PlayerMgr==NULL)
		return false;

	CSingleLock slock(&CPlayerMgr::m_Mutex, true);

	CSKey key(filehash);
	if(g_PlayerMgr->m_TaskLst.IsEmpty())
		return false;

	CPlayerTask * t;
	
	// VC-linhai[2007-08-03]:
	// 将值强制为布尔值“true”或“false”(性能警告)
	//return g_PlayerMgr->m_TaskLst.Lookup(key, t);
	if(g_PlayerMgr->m_TaskLst.Lookup(key, t))
		return true;
	else
		return false;
}

void StopPlayer(const uchar * filehash)
{
	if(g_PlayerMgr==NULL) return;

	CSingleLock slock(&CPlayerMgr::m_Mutex, true);

	CSKey key(filehash);
	g_PlayerMgr->RemovePlayerTask(key);
}

void CPlayerTaskSocket::OnClose(int nErrorCode)
{
	// TODO: 在此添加专用代码和/或调用基类

	CAsyncSocket::OnClose(nErrorCode);
}

void StopAllPlayer()
{
	if(g_PlayerMgr)
	{
		g_bWaitExit = false;

		CPlayerMgr * pOld=g_PlayerMgr;
		g_PlayerMgr = NULL;
		pOld->PostThreadMessage(WM_QUIT, 0, 0);
		while(! g_bWaitExit)
			Sleep(100);
	}
}