VERSION 1.0 CLASS
BEGIN
  MultiUse = -1  'True
  Persistable = 0  'NotPersistable
  DataBindingBehavior = 0  'vbNone
  DataSourceBehavior  = 0  'vbNone
  MTSTransactionMode  = 0  'NotAnMTSObject
END
Attribute VB_Name = "CMyThumbnailProvier"
Attribute VB_GlobalNameSpace = False
Attribute VB_Creatable = True
Attribute VB_PredeclaredId = False
Attribute VB_Exposed = True
'***********************************************************************************************************************
'
'ģ �� ��: CMyThumbnailProvier
'
'��    ��: Supergreenbean
'
'��������: �Զ����ThumbnailProvier
'
'��    ��: 2009��03��10��
'
'ʹ��˵��:
'
'       1. ��IInitializeWithStream_Initialize�л�ð���Ŀ���ļ����ݵ�IStream����
'
'       2. ��IThumbnailProvider_GetThumbnail�д�����������ͼ��λͼ��Ȼ��ֵ��phbmp�����λͼ��Alphaͨ�������趨pdwAlphaΪWTSAT_ARGB
'
'***********************************************************************************************************************
'
Option Explicit
'��ģ������
Private Const THIS_MODULE_NAME As String = "CMyThumbnailProvier"

'--------------------------------------------------------------------
'����ΪWindows Vista�����ϰ汾ʹ�õĽӿڼ�����
'--------------------------------------------------------------------
'ʵ��IInitializeWithStream�ӿ�
Implements IInitializeWithStream
'ʵ��IThumbnailProvider�ӿ�
Implements IThumbnailProvider

'�ļ�IStream����
Private m_pStream As IStream

'--------------------------------------------------------------------
'����ΪWindows XP�����°汾ʹ�õĽӿڼ�����
'--------------------------------------------------------------------
'ʵ��IPersistFile�ӿ�
Implements IPersistFile
'ʵ��IExtractImage�ӿ�
Implements IExtractImage

'�ļ���
Private m_sFileName As String
'�ļ�����
Private m_sFileContents As String

'--------------------------------------------------------------------
'����Ϊ���ñ���
'--------------------------------------------------------------------
'���ص�����ͼλͼ���
Private m_hBmp As Long

'��ȡ�ļ�����
Private Function LoadFileContent() As String
    Dim lFreeFile As Long
    lFreeFile = FreeFile()
    
    m_sFileContents = ""
    Open m_sFileName For Input As #lFreeFile
        Dim sTmpLine As String
        Do While Not EOF(lFreeFile)
            Input #lFreeFile, sTmpLine
            m_sFileContents = m_sFileContents & sTmpLine
        Loop
    Close #lFreeFile
End Function


'���ַ���ָ��õ��ַ���
Private Function StrFromPtr(ByVal lpString As Long, Optional fUnicode As Boolean = True) As String
    On Error Resume Next
    
    If lpString = 0 Then Exit Function
    
    If fUnicode Then
        StrFromPtr = String(lstrlenW(lpString), Chr(0))
        lstrcpyW ByVal StrPtr(StrFromPtr), ByVal lpString
    Else
        StrFromPtr = String(lstrlenA(lpString), Chr(0))
        lstrcpyA ByVal StrFromPtr, ByVal lpString
    End If
End Function

'������
Private Sub Class_Terminate()
    '�ͷ�IStream����
    If Not m_pStream Is Nothing Then Set m_pStream = Nothing
    
    '�ͷ�λͼ����
    If m_hBmp <> 0 Then DeleteObject m_hBmp
End Sub

'--------------------------------------------------------------------
'����ΪWindows Vista�����ϰ汾ʹ�õĽӿ�
'--------------------------------------------------------------------
'ϵͳ�ڳ�ʼ������ͼ����ʱ�ȵ��ô˷������Դ����Ӧ�ļ���IStream����
Private Sub IInitializeWithStream_Initialize(ByVal pstream As ThumbnailProvider.IStream, ByVal grfMode As Long)
    If pstream Is Nothing Then
        Err.Raise E_NOTIMPL
    Else
        Set m_pStream = pstream
    End If
End Sub

'ϵͳ���ô˷����Ի����Ӧ������ͼ
Private Sub IThumbnailProvider_GetThumbnail(ByVal cx As Long, phbmp As Long, pdwAlpha As WTS_ALPHATYPE)
    Dim sFileName As String
    Dim st As STATSTG
    
    'ȡ��IStream�������Ϣ
    m_pStream.Stat st
    
    '��ȡIStream�����������Ϣ
    If st.pwcsName <> 0 Then
        sFileName = StrFromPtr(st.pwcsName, True)
        CoTaskMemFree st.pwcsName
    End If
    
    '����������е��ֽ����ݴ�С
    Dim lFileSize As Long
    'lFileSize = IntegerFromCurrency(st.cbSize)
    lFileSize = st.cbSize.LowPart
    
    If lFileSize <= 0 Then Exit Sub
    
    Dim sBuffer As String
    Dim abBuffer() As Byte
    ReDim abBuffer(lFileSize - 1)
    
    '��ȡ��������
    If m_pStream.Read(abBuffer(0), lFileSize) <= 0 Then Exit Sub
    
    sBuffer = StrConv(abBuffer, vbUnicode)
    
    '��������ͼ
    m_hBmp = CreateThumbnail(sBuffer)
    
    phbmp = m_hBmp
End Sub

'--------------------------------------------------------------------
'����ΪWindows XP�����°汾ʹ�õĽӿ�
'--------------------------------------------------------------------
'��������ͼ
Private Sub IExtractImage_Extract(phBmpThumbnail As Long)
    phBmpThumbnail = m_hBmp
End Sub

'�����ļ��������ȶȵ���Ϣ������Ҫ��ʾ������ͼ�ĳߴ缰ɫ��
Private Sub IExtractImage_GetLocation(ByVal pszPathBuffer As Long, ByVal cch As Long, pdwPriority As Long, prgSize As ThumbnailProvider.SIZE, ByVal dwRecClrDepth As Long, pdwFlags As ThumbnailProvider.IEIFlags)
    If pszPathBuffer <> 0 Then
        PathCreateFromUrl StrPtr(m_sFileName), pszPathBuffer, cch, 0
    End If
    
    pdwPriority = 0
    
    m_hBmp = CreateThumbnailWithSize(m_sFileContents, prgSize.cx, prgSize.cy, dwRecClrDepth)
    
    pdwFlags = IEIFLAG_CACHE
End Sub

Private Sub IPersistFile_GetClassID(pClassID As ThumbnailProvider.VBGUID)
    Err.Raise E_NOTIMPL
End Sub

Private Function IPersistFile_GetCurFile() As Long
    Err.Raise E_NOTIMPL
End Function

Private Sub IPersistFile_IsDirty()
    Err.Raise E_NOTIMPL
End Sub

'ϵͳ���ô˷�������Ŀ�������
Private Sub IPersistFile_Load(ByVal pszFileName As Long, ByVal dwMode As ThumbnailProvider.STGM)
    m_sFileName = StrFromPtr(pszFileName)
    
    Call LoadFileContent
End Sub

Private Sub IPersistFile_Save(ByVal pszFileName As Long, ByVal fRemember As Long)
    Err.Raise E_NOTIMPL
End Sub

Private Sub IPersistFile_SaveCompleted(ByVal pszFileName As Long)
    Err.Raise E_NOTIMPL
End Sub