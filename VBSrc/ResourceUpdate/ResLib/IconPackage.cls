VERSION 1.0 CLASS
BEGIN
  MultiUse = -1  'True
  Persistable = 0  'NotPersistable
  DataBindingBehavior = 0  'vbNone
  DataSourceBehavior  = 0  'vbNone
  MTSTransactionMode  = 0  'NotAnMTSObject
END
Attribute VB_Name = "CIconPackage"
Attribute VB_GlobalNameSpace = False
Attribute VB_Creatable = True
Attribute VB_PredeclaredId = False
Attribute VB_Exposed = True
'/**************************************************************************
'                 ��Ȩ���� (c), 2004 - 2xxx, �̶�������
'
' ************************   ģ �� ��   :CIconPackage********************
'�� �� ��: V1.0
'��    ��: ��Ե
'��������: 2004��03��07��
'����޸�:
'��������:
'�����б�:
'�޸���ʷ:
'��    ��: 2004��03��07��
'�޸���Ա: ��Ե
'�޸�����: ����
'******************************************************************************/
'
Option Explicit
'��ģ������
Private Const THIS_MODULE_NAME As String = "CIconPackage"

Private m_sFilename As String
Private m_oIconEntries As CIconEntries
'/**********************************�� �� ��  : Filename*********************/
'
'��������:
'
'/**************************************************************************/
Friend Property Let Filename(sFilename As String)
    m_sFilename = sFilename
    m_oIconEntries.Filename = m_sFilename
End Property

Public Property Get Filename() As String
     Filename = m_sFilename
End Property

Public Property Get IconEntries() As CIconEntries
    On Error Resume Next
    Set IconEntries = m_oIconEntries
End Property


Private Sub Class_Initialize()
    Set m_oIconEntries = New CIconEntries
End Sub


Private Sub Class_Terminate()
    Set m_oIconEntries = Nothing
End Sub

