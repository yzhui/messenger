#include "stdafx.h"
#include "crypto.h"
#include "session.h"
#include "threads.h"
#include "plugin.h"
#include "main.h"
#include "frmAddrInput.h"

const port_type portListenDefault = 4826;
port_type portListen = portListenDefault;
const char* plugin_file_name = "plugins.txt";

wxBEGIN_EVENT_TABLE(mainFrame, wxFrame)

EVT_LISTBOX(ID_LISTUSER, mainFrame::listUser_SelectedIndexChanged)

EVT_BUTTON(ID_BUTTONADD, mainFrame::buttonAdd_Click)
EVT_BUTTON(ID_BUTTONDEL, mainFrame::buttonDel_Click)

EVT_BUTTON(ID_BUTTONSEND, mainFrame::buttonSend_Click)
EVT_BUTTON(ID_BUTTONSENDIMAGE, mainFrame::buttonSendImage_Click)
EVT_BUTTON(ID_BUTTONSENDFILE, mainFrame::buttonSendFile_Click)
EVT_BUTTON(ID_BUTTONCANCELSEND, mainFrame::buttonCancelSend_Click)

EVT_THREAD(wxID_ANY, mainFrame::thread_Message)

EVT_SIZE(mainFrame::mainFrame_Resize)
EVT_CLOSE(mainFrame::mainFrame_Close)

wxEND_EVENT_TABLE()

#ifdef __WXMSW__
const int _GUI_SIZE_X = 620;
const int _GUI_SIZE_Y = 560;
#else
const int _GUI_SIZE_X = 600;
const int _GUI_SIZE_Y = 540;
#endif

fileSendThread *threadFileSend;
iosrvThread *threadNetwork, *threadMisc;

asio::io_service main_io_service, misc_io_service;
std::unique_ptr<wx_srv_interface> srv;

std::unordered_map<user_id_type, user_ext_type> user_ext;
std::unordered_map<plugin_id_type, plugin_info_type> plugin_info;
std::unordered_set<user_id_type> virtual_users;

void plugin_handler_SendData(plugin_id_type plugin_id, int to, const char* data, size_t size)
{
	if (!plugin_check_id_type(plugin_id, *data))
		return;
	std::shared_ptr<std::string> data_str = std::make_shared<std::string>(data, size);
	if (to == -1)
	{
		for (const std::pair<user_id_type, user_ext_type> &p : user_ext)
		{
			user_id_type id = p.first;
			misc_io_service.post([id, data_str]() {
				srv->send_data(id, *data_str, msgr_proto::session::priority_plugin);
			});
		};
	}
	else if (to >= 0 && to <= std::numeric_limits<plugin_id_type>::max())
	{
		misc_io_service.post([to, data_str]() {
			srv->send_data(static_cast<user_id_type>(to), *data_str, msgr_proto::session::priority_plugin);
		});
	}
}

void plugin_handler_ConnectTo(uint32_t addr, uint16_t port)
{
	srv->connect(addr, port);
}

int plugin_handler_NewVirtualUser(plugin_id_type plugin_id, const char* name)
{
	try
	{
		plugin_info_type &info = plugin_info.at(plugin_id);
		plugin_info_type::virtual_msg_handler_ptr virtual_msg_handler = info.virtual_msg_handler;
		std::string name_str = info.name;
		if (name[0] != '\0')
		{
			name_str.push_back(':');
			name_str.append(name);
		}

		std::shared_ptr<msgr_proto::virtual_session> new_session = std::make_shared<msgr_proto::virtual_session>(*srv, name_str);
		if (virtual_msg_handler == nullptr)
			new_session->set_callback([](const std::string&) {});
		else
		{
			new_session->set_callback([new_session, virtual_msg_handler](const std::string& data) {
				virtual_msg_handler(new_session->get_id(), data.data(), data.size());
			});
		}
		new_session->join();
		new_session->start();
		user_id_type new_user_id = new_session->get_id();
		info.virtual_user_list.emplace(new_user_id);
		virtual_users.emplace(new_user_id);
		return new_user_id;
	}
	catch (...) {}
	return -1;
}

bool plugin_handler_DelVirtualUser(plugin_id_type plugin_id, uint16_t virtual_user_id)
{
	try
	{
		std::unordered_set<uint16_t> &virtual_user_list = plugin_info.at(plugin_id).virtual_user_list;
		std::unordered_set<uint16_t>::iterator itr = virtual_user_list.find(virtual_user_id);
		if (itr != virtual_user_list.end())
		{
			srv->leave(virtual_user_id);
			virtual_user_list.erase(itr);
			return true;
		}
	}
	catch (...) {}
	return false;
}

bool plugin_handler_VirtualUserMsg(plugin_id_type plugin_id, uint16_t virtual_user_id, const char* message, uint32_t size)
{
	try
	{
		plugin_info_type &info = plugin_info.at(plugin_id);
		if (info.virtual_user_list.find(virtual_user_id) != info.virtual_user_list.end())
		{
			std::dynamic_pointer_cast<msgr_proto::virtual_session>(srv->get_session(virtual_user_id))->push(std::string(message, size));
			return true;
		}
	}
	catch (...) {}
	return false;
}

std::string uid_global;
const char* plugin_method_GetUserID()
{
	return uid_global.c_str();
}

void plugin_method_Print(plugin_id_type plugin_id, const char* msg)
{
	try
	{
		plugin_info_type &info = plugin_info.at(plugin_id);
		std::cout << "Plugin:" << info.name << ':' << msg << std::endl;
	}
	catch (...) {}
}

const wxPoint itemPos[] = {
	{},
	{ 12,  12  },	//LABEL_LISTUSER
	{ 12,  39  },	//LISTUSER
	{ 12,  321 },	//BUTTONADD
	{ 12,  369 },	//BUTTONDEL
	{ 180, 12  },	//TEXTMSG
	{ 180, 321 },	//TEXTINPUT
	{ 180, 369 },	//BUTTONSEND
	{ 284, 369 },	//BUTTONSENDIMAGE
	{ 389, 369 },	//BUTTONSENDFILE
	{ 494, 369 },	//BUTTONCANCELSEND
	{ 12,  417 },	//TEXTINFO
};

const wxSize itemSize[] = {
	{},
	{ 162, 21  },	//LABEL_LISTUSER
	{ 162, 276 },	//LISTUSER
	{ 162, 42  },	//BUTTONADD
	{ 162, 42  },	//BUTTONDEL
	{ 412, 303 },	//TEXTMSG
	{ 412, 42  },	//TEXTINPUT
	{ 98,  42  },	//BUTTONSEND
	{ 99,  42  },	//BUTTONSENDIMAGE
	{ 99,  42  },	//BUTTONSENDFILE
	{ 98,  42  },	//BUTTONCANCELSEND
	{ 580, 92  },	//TEXTINFO
};

mainFrame::mainFrame(const wxString& title)
	: wxFrame(NULL, ID_FRAME, title, wxDefaultPosition, wxSize(_GUI_SIZE_X, _GUI_SIZE_Y))
{
	Center();

	panel = new wxPanel(this, wxID_ANY, wxPoint(0, 0), wxSize(_GUI_SIZE_X, _GUI_SIZE_Y));

	labelListUser = new wxStaticText(panel, ID_LABELLISTUSER,
		wxT("User list"),
		itemPos[ID_LABELLISTUSER],
		itemSize[ID_LABELLISTUSER]
		);
	listUser = new wxListBox(panel, ID_LISTUSER,
		itemPos[ID_LISTUSER],
		itemSize[ID_LISTUSER],
		wxArrayString()
		);
	buttonAdd = new wxButton(panel, ID_BUTTONADD,
		wxT("Connect to"),
		itemPos[ID_BUTTONADD],
		itemSize[ID_BUTTONADD]
		);
	buttonDel = new wxButton(panel, ID_BUTTONDEL,
		wxT("Disconnect"),
		itemPos[ID_BUTTONDEL],
		itemSize[ID_BUTTONDEL]
		);

	textMsg = new wxRichTextCtrl(panel, ID_TEXTMSG,
		wxEmptyString,
		itemPos[ID_TEXTMSG],
		itemSize[ID_TEXTMSG],
		wxTE_MULTILINE | wxTE_READONLY
		);
	textInput = new wxTextCtrl(panel, ID_TEXTINPUT,
		wxEmptyString,
		itemPos[ID_TEXTINPUT],
		itemSize[ID_TEXTINPUT],
		wxTE_MULTILINE
		);
	buttonSend = new wxButton(panel, ID_BUTTONSEND,
		wxT("Send"),
		itemPos[ID_BUTTONSEND],
		itemSize[ID_BUTTONSEND]
		);
	buttonSendImage = new wxButton(panel, ID_BUTTONSENDIMAGE,
		wxT("Image"),
		itemPos[ID_BUTTONSENDIMAGE],
		itemSize[ID_BUTTONSENDIMAGE]
		);
	buttonSendFile = new wxButton(panel, ID_BUTTONSENDFILE,
		wxT("File"),
		itemPos[ID_BUTTONSENDFILE],
		itemSize[ID_BUTTONSENDFILE]
		);
	buttonCancelSend = new wxButton(panel, ID_BUTTONCANCELSEND,
		wxT("Cancel"),
		itemPos[ID_BUTTONCANCELSEND],
		itemSize[ID_BUTTONCANCELSEND]
		);

	textInfo = new wxTextCtrl(panel, ID_TEXTINFO,
		wxEmptyString,
		itemPos[ID_TEXTINFO],
		itemSize[ID_TEXTINFO],
		wxTE_MULTILINE | wxTE_READONLY
		);

	const int entry_count = 1;
	wxAcceleratorEntry entries[entry_count];
	entries[0].Set(wxACCEL_CTRL, WXK_RETURN, ID_BUTTONSEND);
	wxAcceleratorTable accel(entry_count, entries);
	SetAcceleratorTable(accel);

	textStrm = new textStream(this, textInfo);
	cout_orig = std::cout.rdbuf();
	std::cout.rdbuf(textStrm);
	cerr_orig = std::cerr.rdbuf();
	std::cerr.rdbuf(textStrm);

	if (fs::exists(plugin_file_name))
	{
		plugin_init();
		uid_global.assign(getUserIDGlobal());
		set_method("GetUserID", reinterpret_cast<void*>(plugin_method_GetUserID));
		set_method("Print", reinterpret_cast<void*>(plugin_method_Print));

		set_handler(ExportHandlerID::SendDataHandler, reinterpret_cast<void*>(plugin_handler_SendData));
		set_handler(ExportHandlerID::ConnectToHandler, reinterpret_cast<void*>(plugin_handler_ConnectTo));

		set_method("NewUser", reinterpret_cast<void*>(plugin_handler_NewVirtualUser));
		set_method("DelUser", reinterpret_cast<void*>(plugin_handler_DelVirtualUser));
		set_method("UserMsg", reinterpret_cast<void*>(plugin_handler_VirtualUserMsg));

		std::ifstream fin(plugin_file_name);
		std::string plugin_path_utf8;
		while (!fin.eof())
		{
			std::getline(fin, plugin_path_utf8);
			if (!plugin_path_utf8.empty())
			{
				std::wstring plugin_path(wxConvUTF8.cMB2WC(plugin_path_utf8.c_str()));
				int plugin_id_int = load_plugin(plugin_path);
				if (plugin_id_int != -1)
				{
					plugin_id_type plugin_id = static_cast<plugin_id_type>(plugin_id_int);
					plugin_info_type &info = plugin_info.emplace(plugin_id, plugin_info_type()).first->second;
					info.name = fs::path(plugin_path).filename().stem().string();
					info.plugin_id = plugin_id;
					info.virtual_msg_handler = reinterpret_cast<plugin_info_type::virtual_msg_handler_ptr>(get_callback(plugin_id, "OnUserMsg"));
				}
			}
		}
	}
}

#define REPOSITION(control, id) (control)->SetPosition(wxPoint(static_cast<int>(itemPos[id].x * ratio_x), static_cast<int>(itemPos[id].y * ratio_y)))
#define RESIZE(control, id) (control)->SetSize(wxSize(static_cast<int>(itemSize[id].x * ratio_x), static_cast<int>(itemSize[id].y * ratio_y)))

void mainFrame::mainFrame_Resize(wxSizeEvent& event)
{
	panel->SetSize(wxSize(event.GetSize().GetWidth(), event.GetSize().GetHeight()));
	double ratio_x = event.GetSize().GetWidth(), ratio_y = event.GetSize().GetHeight();
	ratio_x /= _GUI_SIZE_X;
	ratio_y /= _GUI_SIZE_Y;

	REPOSITION(labelListUser, ID_LABELLISTUSER);
	REPOSITION(listUser, ID_LISTUSER);
	REPOSITION(buttonAdd, ID_BUTTONADD);
	REPOSITION(buttonDel, ID_BUTTONDEL);
	REPOSITION(textMsg, ID_TEXTMSG);
	REPOSITION(textInput, ID_TEXTINPUT);
	REPOSITION(buttonSend, ID_BUTTONSEND);
	REPOSITION(buttonSendImage, ID_BUTTONSENDIMAGE);
	REPOSITION(buttonSendFile, ID_BUTTONSENDFILE);
	REPOSITION(buttonCancelSend, ID_BUTTONCANCELSEND);
	REPOSITION(textInfo, ID_TEXTINFO);

	RESIZE(labelListUser, ID_LABELLISTUSER);
	RESIZE(listUser, ID_LISTUSER);
	RESIZE(buttonAdd, ID_BUTTONADD);
	RESIZE(buttonDel, ID_BUTTONDEL);
	RESIZE(textMsg, ID_TEXTMSG);
	RESIZE(textInput, ID_TEXTINPUT);
	RESIZE(buttonSend, ID_BUTTONSEND);
	RESIZE(buttonSendImage, ID_BUTTONSENDIMAGE);
	RESIZE(buttonSendFile, ID_BUTTONSENDFILE);
	RESIZE(buttonCancelSend, ID_BUTTONCANCELSEND);
	RESIZE(textInfo, ID_TEXTINFO);
}

#undef RESIZE
#undef REPOSITION

void mainFrame::listUser_SelectedIndexChanged(wxCommandEvent& event)
{
	user_id_type uID = userIDs[listUser->GetSelection()];
	textMsg->Clear();
	std::for_each(user_ext[uID].log.begin(), user_ext[uID].log.end(), [this](const user_ext_type::log_type &log_item) {
		if (log_item.is_image)
			textMsg->WriteImage(log_item.image.native(), wxBITMAP_TYPE_ANY);
		else
			textMsg->AppendText(log_item.msg);
	});
	textMsg->ShowPosition(textMsg->GetLastPosition());
}

void mainFrame::buttonAdd_Click(wxCommandEvent& event)
{
	try
	{
		frmAddrInput inputDlg(wxT("Please input address"), portListen);
		if (inputDlg.ShowModal() != wxID_OK || inputDlg.CheckInput() == false)
			return;
		srv->connect(inputDlg.GetAddress().ToStdString(), static_cast<port_type>(inputDlg.GetPort()));
	}
	catch (std::exception &ex)
	{
		textInfo->AppendText(ex.what() + std::string("\n"));
	}
}

void mainFrame::buttonDel_Click(wxCommandEvent& event)
{
	int selection = listUser->GetSelection();
	if (selection != -1)
	{
		if (virtual_users.find(userIDs[selection]) == virtual_users.end())
			srv->disconnect(userIDs[selection]);
	}
}

void mainFrame::buttonSend_Click(wxCommandEvent& event)
{
	wxString msg = textInput->GetValue();
	if (!msg.empty())
	{
		textInput->SetValue(wxEmptyString);
		if (listUser->GetSelection() != -1)
		{
			wxCharBuffer buf = wxConvUTF8.cWC2MB(msg.c_str());
			std::string msg_utf8(buf, buf.length());
			user_id_type uID = userIDs[listUser->GetSelection()];
			insLen(msg_utf8);
			msg_utf8.insert(0, 1, PAC_TYPE_MSG);
			misc_io_service.post([uID, msg_utf8]() {
				srv->send_data(uID, msg_utf8, msgr_proto::session::priority_msg);
			});
			textMsg->AppendText("Me:" + msg + '\n');
			user_ext[uID].log.push_back("Me:" + msg + '\n');
			textMsg->ShowPosition(textMsg->GetLastPosition());
		}
	}
}

void mainFrame::buttonSendImage_Click(wxCommandEvent& event)
{
	if (listUser->GetSelection() != -1)
	{
		user_id_type uID = userIDs[listUser->GetSelection()];
		wxFileDialog fileDlg(this, wxT("Image"), wxEmptyString, wxEmptyString, "Image files (*.bmp;*.jpg;*.jpeg;*.gif;*.png)|*.bmp;*.jpg;*.jpeg;*.gif;*.png");
		fileDlg.ShowModal();
		wxString path = fileDlg.GetPath();
		if ((!path.empty()) && fs::is_regular_file(path.ToStdWstring()))
		{
			if (fs::file_size(path.ToStdWstring()) > IMAGE_SIZE_LIMIT)
			{
				wxMessageBox(wxT("Image file is too big"), wxT("Error"), wxOK | wxICON_ERROR);
				return;
			}
			int next_image_id;
			srv->new_image_id(next_image_id);
			fs::path image_path = IMG_TMP_PATH_NAME;
			image_path /= std::to_string(uID);
			image_path /= ".messenger_tmp_" + std::to_string(next_image_id);
			fs::copy_file(path.ToStdWstring(), image_path);

			wxImage image(path, wxBITMAP_TYPE_ANY);
			if (image.IsOk())
			{
				textMsg->AppendText("Me:\n");
				textMsg->WriteImage(image_path.native(), wxBITMAP_TYPE_ANY);
				textMsg->AppendText("\n");
				textMsg->ShowPosition(textMsg->GetLastPosition());

				user_ext[uID].log.push_back("Me:\n");
				user_ext[uID].log.push_back(image_path);
				user_ext[uID].log.push_back("\n");

				std::shared_ptr<std::string> img_buf = std::make_shared<std::string>();

				std::ifstream fin(path.ToStdString(), std::ios_base::in | std::ios_base::binary);
				std::unique_ptr<char[]> read_buf = std::make_unique<char[]>(fileSendThread::fileBlockLen);
				while (!fin.eof())
				{
					fin.read(read_buf.get(), fileSendThread::fileBlockLen);
					img_buf->append(read_buf.get(), fin.gcount());
				}
				fin.close();
				insLen(*img_buf);
				img_buf->insert(0, 1, PAC_TYPE_IMAGE);

				srv->send_data(uID, std::move(*img_buf), msgr_proto::session::priority_msg);
			}
		}
	}
}

void mainFrame::buttonSendFile_Click(wxCommandEvent& event)
{
	if (listUser->GetSelection() != -1)
	{
		user_id_type uID = userIDs[listUser->GetSelection()];
		wxFileDialog fileDlg(this);
		fileDlg.ShowModal();
		std::wstring path = fileDlg.GetPath().ToStdWstring();
		if ((!path.empty()) && fs::exists(path))
			threadFileSend->start(uID, fs::path(path));
	}
}

void mainFrame::buttonCancelSend_Click(wxCommandEvent& event)
{
	if (listUser->GetSelection() != -1)
	{
		user_id_type uID = userIDs[listUser->GetSelection()];
		threadFileSend->stop(uID);
	}
}

void mainFrame::thread_Message(wxThreadEvent& event)
{
	try
	{
		event.GetPayload<gui_callback>()();
	}
	catch (...) {}
}

void mainFrame::mainFrame_Close(wxCloseEvent& event)
{
	try
	{
		std::cout.rdbuf(cout_orig);
		std::cerr.rdbuf(cerr_orig);
		delete textStrm;

		srv->set_frame(nullptr);
	}
	catch (std::exception &ex)
	{
		wxMessageBox(ex.what(), wxT("Error"), wxOK | wxICON_ERROR);
	}
	wxFrame::OnCloseWindow(event);
}

IMPLEMENT_APP(MyApp)

bool MyApp::OnInit()
{
	int stage = 0;
	try
	{
		wxImage::AddHandler(new wxPNGHandler);
		wxImage::AddHandler(new wxJPEGHandler);
		wxImage::AddHandler(new wxGIFHandler);
		if (fs::exists(IMG_TMP_PATH_NAME))
			fs::remove_all(IMG_TMP_PATH_NAME);
		fs::create_directories(IMG_TMP_PATH_NAME);

		port_type portsBegin = 5000, portsEnd = 9999;
		bool use_v6 = false;

		threadNetwork = new iosrvThread(main_io_service);
		stage = 1;
		threadMisc = new iosrvThread(misc_io_service);
		stage = 2;

		initKey();

		srv = std::make_unique<wx_srv_interface>(main_io_service, misc_io_service,
			asio::ip::tcp::endpoint((use_v6 ? asio::ip::tcp::v6() : asio::ip::tcp::v4()), portListen));

		for (int i = 1; i < argc; i++)
		{
			std::string arg(argv[i]);
			if (arg.substr(0, 5) == "port=")
			{
				portListen = static_cast<port_type>(std::stoi(arg.substr(5)));
			}
			else if (arg.substr(0, 6) == "ports=")
			{
				int pos = arg.find('-', 6);
				if (pos == std::string::npos)
				{
					srv->set_static_port(static_cast<port_type>(std::stoi(arg.substr(6))));
					portsBegin = 1;
					portsEnd = 0;
				}
				else
				{
					std::string ports_begin = arg.substr(6, pos - 6), ports_end = arg.substr(pos + 1);
					portsBegin = static_cast<port_type>(std::stoi(ports_begin));
					portsEnd = static_cast<port_type>(std::stoi(ports_end));
					srv->set_static_port(-1);
				}
			}
			else if (arg == "usev6")
			{
				use_v6 = true;
			}
			else
			{
				throw(std::invalid_argument("Invalid argument:" + arg));
			}
		}
		
		std::srand(static_cast<unsigned int>(std::time(NULL)));
		for (; portsBegin <= portsEnd; portsBegin++)
			srv->free_rand_port(portsBegin);

		srv->start();

		threadFileSend = new fileSendThread(*srv);
		stage = 3;

		form = new mainFrame(wxT("Messenger"));
		form->Show();
		srv->set_frame(form);

		if (threadNetwork->Run() != wxTHREAD_NO_ERROR)
		{
			delete threadNetwork;
			throw(std::runtime_error("Can't run iosrvThread"));
		}
		if (threadMisc->Run() != wxTHREAD_NO_ERROR)
		{
			delete threadMisc;
			throw(std::runtime_error("Can't run iosrvThread"));
		}
		if (threadFileSend->Run() != wxTHREAD_NO_ERROR)
		{
			delete threadFileSend;
			throw(std::runtime_error("Can't run fileSendThread"));
		}
	}
	catch (std::exception &ex)
	{
		switch (stage)
		{
			case 3:
				threadFileSend->Delete();
			case 2:
				threadMisc->Delete();
			case 1:
				threadNetwork->Delete();
			default:
				break;
		}

		wxMessageBox(ex.what(), wxT("Error"), wxOK | wxICON_ERROR);
		return false;
	}

	return true;
}

int MyApp::OnExit()
{
	try
	{
		threadFileSend->Delete();
		threadMisc->Delete();
		threadNetwork->Delete();

		srv.reset();

		fs::remove_all(IMG_TMP_PATH_NAME);
	}
	catch (...)
	{
		return 1;
	}

	return 0;
}
