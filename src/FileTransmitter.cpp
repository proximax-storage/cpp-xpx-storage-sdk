/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "FileTransmitter.h"

// libtorrent
#include <libtorrent/alert.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/hex.hpp>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/torrent_flags.hpp>
#include <libtorrent/extensions/ut_metadata.hpp>

// std
#include <iostream>

// boost
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

namespace xpx_storage_sdk {

//DefaultFileTransmitter
class DefaultFileTransmitter : public FileTransmitter {
private:
    lt::session mSession;
    lt::file_storage mFileStorage;

    std::map<lt::sha256_hash, DownloadFileHandler> mTorrentHandlers;

private:
    static std::string makeMagnetURI(const FileHash& hash) {
        const std::string magnetPart = "magnet:?xt=urn:btmh:1220";
        const std::string hashPart(std::begin(hash), std::end(hash));

        return magnetPart + hashPart;
    }

    static FileHash toHash(const lt::sha256_hash& hash) {
        FileHash finalHash;
        std::copy(hash.begin(), hash.end(), std::begin(finalHash));

        return finalHash;
    }

    static lt::sha256_hash toLtHash(const FileHash& hash) {
        lt::sha256_hash finalHash;
        std::copy(hash.begin(), hash.end(), std::begin(finalHash));

        return finalHash;
    }

    void alertHandler() {
        std::vector<lt::alert *> alerts;
        mSession.pop_alerts(&alerts);

        // TODO: check errors, alerts
        for (auto &alert : alerts) {
            switch (alert->type()) {
                case lt::torrent_finished_alert::alert_type: {
                    auto *pa = dynamic_cast<lt::torrent_finished_alert *>(alert);
                    if (pa) {
                        const lt::sha256_hash ltHash = pa->handle.info_hashes().v2;
                        auto findResult = mTorrentHandlers.find(ltHash);
                        if (findResult != mTorrentHandlers.end()) {
//                        if (mTorrentHandlers.contains(ltHash)) {

                            const std::string fileName = pa->handle.torrent_file()->files().file_name(0).to_string();
                            mSession.remove_torrent(pa->handle);

                            DownloadFileHandler handler = mTorrentHandlers[ltHash];
                            handler(download_status::complete, toHash(ltHash), fileName);

                            mTorrentHandlers.erase(ltHash);
                        }
                    }
                    break;
                }
//                case lt::piece_finished_alert::alert_type: {
//                    auto *pa = dynamic_cast<lt::piece_finished_alert *>(alert);
//                    if (pa) {
//                        const lt::sha256_hash ltHash = pa->handle.torrent_file()->info_hashes().v2;
//                        std::cout << "alert: curr piece" << pa->piece_index << std::endl;
//                        std::cout << "alert: num pieces" << pa->handle.torrent_file()->num_pieces() << std::endl;
//                        std::vector<int64_t> fp;
//                        pa->handle.file_progress(fp, lt::torrent_handle::piece_granularity);
//                        std::cout << "alert: my progress: " << fp << std::endl;
//                    }
//
//                    std::cout << "alert: my piece finished: " << alert->message() << std::endl;
//                    break;
//                }
//                case lt::file_completed_alert::alert_type: {
//                    //auto *pa = dynamic_cast<lt::file_completed_alert *>(alert);
//
//                    std::cout << "alert: file completed: " << alert->message() << std::endl;
//                    break;
//                }
                default: {
                    std::cout << "alert: " << alert->message() << std::endl;
                }
            }
        }
    }

public:
    DefaultFileTransmitter() {}

    virtual ~DefaultFileTransmitter() {}

    virtual void init( const std::string& address ) {
        lt::settings_pack settingsPack;

        settingsPack.set_int(lt::settings_pack::alert_mask, lt::alert_category::all);
        settingsPack.set_str(lt::settings_pack::dht_bootstrap_nodes, "");

        boost::uuids::uuid uuid = boost::uuids::random_generator()();

        settingsPack.set_str(lt::settings_pack::user_agent, boost::uuids::to_string(uuid));
        settingsPack.set_bool(lt::settings_pack::enable_dht, true);
        settingsPack.set_bool(lt::settings_pack::enable_lsd, true);
        settingsPack.set_str(lt::settings_pack::listen_interfaces, address);
        settingsPack.set_bool(lt::settings_pack::allow_multiple_connections_per_ip, true);

        mSession.apply_settings(settingsPack);
        mSession.set_alert_notify([this] { alertHandler(); });
    }

    FileHash prepareActionListToUpload( const ActionList&, std::string addr, int port) override {

        return FileHash();
    }

    void download( FileHash hash, const std::string& outputFolder, DownloadFileHandler handler, const std::string& address, unsigned short port ) override {
        if (outputFolder.empty()) {
            handler(download_status::failed, hash, "");
            return;
        }

        if (address.empty()) {
            handler(download_status::failed, hash, "");
            return;
        }

        const std::string magnetLink = makeMagnetURI(hash);

        lt::error_code ec;
        lt::add_torrent_params tp = lt::parse_magnet_uri(magnetLink, ec);
        if (ec.value() != 0) {
            handler(download_status::failed, hash, "");
            return;
        }

        tp.save_path = outputFolder;

        lt::torrent_handle th = mSession.add_torrent(tp, ec);
        if (ec.value() != 0) {
            handler(download_status::failed, hash, "");
            return;
        }

        if (mSession.is_valid() && th.is_valid()) {
            mTorrentHandlers.insert(std::pair<lt::sha256_hash, DownloadFileHandler>(toLtHash(hash), handler));

            lt::tcp::endpoint endpoint;
            endpoint.address(boost::asio::ip::make_address(address));
            endpoint.port(port);

            th.connect_peer(endpoint);
        } else {
            handler(download_status::failed, hash, "");
        }
    }

    void addFile( Key drivePubKey, std::string fileNameWithPath, ErrorHandler handler )  override {
        lt::add_files(mFileStorage, fileNameWithPath);

        const int piece_size = 16;
        lt::create_torrent t(mFileStorage, piece_size, lt::create_torrent::v2_only);
        lt::set_piece_hashes(t, "./files");

        std::vector<char> buf;
        lt::bencode(std::back_inserter(buf), t.generate());

        lt::add_torrent_params tp;
        tp.flags &= ~lt::torrent_flags::paused;
        tp.flags &= ~lt::torrent_flags::auto_managed;
        tp.storage_mode = lt::storage_mode_sparse;
        tp.save_path = "./files";
        tp.ti = std::make_shared<lt::torrent_info>(buf, lt::from_span);

        lt::error_code ec;
        mSession.add_torrent(tp);
        if (ec.value() != 0) {
            handler(error::failed, ec.message());
            return;
        }
    }

    void removeFile( Key drivePubKey, FileHash, std::string fileNameWithPath, ErrorHandler )  override {

    }

};

std::shared_ptr<FileTransmitter> createDefaultFileTransmitter() {

    return std::make_shared<DefaultFileTransmitter>();
}
}
