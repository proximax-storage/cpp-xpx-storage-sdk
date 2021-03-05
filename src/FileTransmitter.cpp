/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "FileTransmitter.h"

#include <libtorrent/session.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/hex.hpp>
#include <iostream>

namespace xpx_storage_sdk {

//DefaultFileTransmitter
class DefaultFileTransmitter : public FileTransmitter {
private:
    lt::session mSession;

private:
    static std::string makeMagnetURI(const FileHash& hash)
    {
        std::string res = "magnet:?xt=urn:btmh:1220";
        res += lt::aux::to_hex(*hash.data());

        return res;
    }

public:
    DefaultFileTransmitter() {}

    virtual ~DefaultFileTransmitter() {}

    virtual void init(unsigned short port = 0) {
        lt::settings_pack settingsPack;

        settingsPack.set_int(lt::settings_pack::alert_mask, lt::alert_category::all);
        settingsPack.set_str(lt::settings_pack::dht_bootstrap_nodes, "10.0.0.14:6900");
        settingsPack.set_str(lt::settings_pack::dht_bootstrap_nodes, "");
        settingsPack.set_str(lt::settings_pack::user_agent, "torrent-client-v0.0.1");
        settingsPack.set_bool(lt::settings_pack::enable_dht, true);
        settingsPack.set_bool(lt::settings_pack::enable_lsd, true);
        settingsPack.set_str(lt::settings_pack::listen_interfaces, "10.0.0.15:6800");
        settingsPack.set_bool(lt::settings_pack::allow_multiple_connections_per_ip, true);

        mSession.apply_settings(settingsPack);
        mSession.set_alert_notify([&]() {
            std::vector<lt::alert *> alerts;

            mSession.pop_alerts(&alerts);

            for (auto &alert : alerts) {
                std::cout << "alert msg: " << alert->message() << std::endl;
                std::cout << "alert msg: " << alert->what() << std::endl;
            }
        });
    }

    FileHash prepareActionListToUpload( const ActionList&, std::string addr, int port) override {

        return FileHash();
    }

    void download( FileHash hash, const std::string& outputFolder, DownloadFileHandler, const std::string& addr = "", unsigned short port = 0 ) override {

        if( outputFolder.empty() ) {
            return;
        }

        if( addr.empty() ) {
            return;
        }

        if( port <= 1024 ) {
            return;
        }

        // "magnet:?xt=urn:btmh:1220fa63b7c86a9b6086d9640246fbf3a1dd127db8dc01065976a085bdde25f0f101";
        const std::string magnetLink = makeMagnetURI(hash);

        lt::add_torrent_params p = lt::parse_magnet_uri(magnetLink);
        p.save_path = outputFolder;

        lt::torrent_handle th = mSession.add_torrent(p);

        lt::tcp::endpoint endpoint;
        endpoint.address(boost::asio::ip::make_address(addr));
        endpoint.port(port);

        th.connect_peer(endpoint);

//        std::vector<std::int64_t> const file_progress = th.file_progress();
//        std::vector<lt::open_file_state> file_status = th.file_status();
//        std::vector<lt::download_priority_t> file_prio = th.get_file_priorities();
//        auto f = file_status.begin();
//        std::shared_ptr<const lt::torrent_info> ti = th.torrent_file();
//
//        int p = 0; // this is horizontal position
//        for (lt::file_index_t i(0); i < lt::file_index_t(ti->num_files()); ++i)
//        {
//            auto const idx = std::size_t(static_cast<int>(i));
//            if (pos + 1 >= terminal_height) break;
//
//            bool const pad_file = ti->files().pad_file_at(i);
//            if (pad_file && !show_pad_files) continue;
//
//            int const progress = ti->files().file_size(i) > 0
//                                 ? int(file_progress[idx] * 1000 / ti->files().file_size(i)) : 1000;
//            assert(file_progress[idx] <= ti->files().file_size(i));
//
//            bool const complete = file_progress[idx] == ti->files().file_size(i);
//
//            std::string title = ti->files().file_name(i).to_string();
//            if (!complete)
//            {
//                std::snprintf(str, sizeof(str), " (%.1f%%)", progress / 10.0);
//                title += str;
//            }
//
//            if (f != file_status.end() && f->file_index == i)
//            {
//                title += " [ ";
//                if ((f->open_mode & lt::file_open_mode::rw_mask) == lt::file_open_mode::read_write) title += "read/write ";
//                else if ((f->open_mode & lt::file_open_mode::rw_mask) == lt::file_open_mode::read_only) title += "read ";
//                else if ((f->open_mode & lt::file_open_mode::rw_mask) == lt::file_open_mode::write_only) title += "write ";
//                if (f->open_mode & lt::file_open_mode::random_access) title += "random_access ";
//                if (f->open_mode & lt::file_open_mode::sparse) title += "sparse ";
//                title += "]";
//                ++f;
//            }
//
//            const int file_progress_width = pad_file ? 10 : 65;
//
//            // do we need to line-break?
//            if (p + file_progress_width + 13 > terminal_width)
//            {
//                out += "\x1b[K\n";
//                pos += 1;
//                p = 0;
//            }
//
//            std::snprintf(str, sizeof(str), "%s %7s p: %d ",
//                          progress_bar(progress, file_progress_width
//                                  , pad_file ? col_blue
//                                             : complete ? col_green : col_yellow
//                                  , '-', '#', title.c_str()).c_str()
//                    , add_suffix(file_progress[idx]).c_str()
//                    , static_cast<std::uint8_t>(file_prio[idx]));
//
//            p += file_progress_width + 13;
//            out += str;
//        }


    }

    void addFile( Key drivePubKey, FileHash, std::string fileNameWithPath, ErrorHandler )  override {

    }

    void removeFile( Key drivePubKey, FileHash, std::string fileNameWithPath, ErrorHandler )  override {

    }

};

std::shared_ptr<FileTransmitter> createDefaultFileTransmitter() {

    return std::make_shared<DefaultFileTransmitter>();
}
}
