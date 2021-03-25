#include "libtorrent/entry.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/torrent_info.hpp"

#include <iostream>

int main(int, char*[]) try
{
    lt::settings_pack sessionSettings;

    sessionSettings.set_int(lt::settings_pack::alert_mask, lt::alert_category::all);
    sessionSettings.set_str(lt::settings_pack::dht_bootstrap_nodes, "10.0.0.14:6900");
    sessionSettings.set_str(lt::settings_pack::dht_bootstrap_nodes, "");
    sessionSettings.set_str(lt::settings_pack::user_agent, "torrent-client-v0.0.1");
    sessionSettings.set_bool(lt::settings_pack::enable_dht, true);
    sessionSettings.set_bool(lt::settings_pack::enable_lsd, true);
    sessionSettings.set_str(lt::settings_pack::listen_interfaces, "10.0.0.15:6800");
    sessionSettings.set_bool(lt::settings_pack::allow_multiple_connections_per_ip, true);

    lt::session s;
    s.apply_settings(sessionSettings);
    s.set_alert_notify([&s]()
    {
        std::vector<lt::alert *> alerts;

        s.pop_alerts(&alerts);

        for (auto i = 0u; i < alerts.size(); i++)
        {
            std::cout << "alert msg: " << alerts[i]->message() << std::endl;
            std::cout << "alert msg: " << alerts[i]->what() << std::endl;
        }
    });

    lt::add_torrent_params p;
    p.save_path = "./files";
    //std::string torrentFile = "/home/cempl/projects/torrent-peer/cmake-build-debug/files/bc.torrent";
    //std::string torrentFile = "./files/external.torrent";
    std::string torrentFile = "./files/bc.log.torrent";
    p.ti = std::make_shared<lt::torrent_info>(torrentFile);
    lt::torrent_handle th = s.add_torrent(p);
//    lt::sha256_hash torrentHash = th.info_hashes().v2;

    lt::tcp::endpoint endpoint;
    endpoint.address(boost::asio::ip::make_address("10.0.0.14"));
    endpoint.port(6881);

    std::cout << "is valid: " << th.is_valid() << std::endl;

//    std::vector<lt::peer_info> peerInfo;
//    th.get_peer_info(peerInfo);

    th.connect_peer(endpoint);

    const int timeout = 5;
    for (int i = 0; i < timeout; ++i)
    {
        std::this_thread::sleep_for(lt::milliseconds(500));

        lt::torrent_status st1 = th.status();
        if (st1.is_finished)
        {
            s.abort();
            break;
        }
    }

    return 0;
}
catch (std::exception const& e) {
    std::cerr << "ERROR: " << e.what() << "\n";
}



//#include <iostream>
//#include "libtorrent/entry.hpp"
//#include "libtorrent/session.hpp"
//#include "libtorrent/torrent_info.hpp"
//
//int main(int argc, char* argv[]) {
//    lt::add_torrent_params p;
//    p.save_path = ".";
//    //std::string fileName = "/home/cempl/projects/torrent-peer/cmake-build-debug/files/bc.torrent";
//    std::string fileName = "./files/external.torrent";
//
//    p.ti = std::make_shared<lt::torrent_info>(fileName);
//
//    lt::settings_pack sessionSettings;
//    sessionSettings.set_int(lt::settings_pack::alert_mask, lt::alert_category::all);
//    sessionSettings.set_str(lt::settings_pack::listen_interfaces, "127.0.0.1:6800");
//    sessionSettings.set_str(lt::settings_pack::handshake_client_version, "torrent-client");
//    sessionSettings.set_str(lt::settings_pack::peer_fingerprint, "torrent-client");
//    sessionSettings.set_str(lt::settings_pack::user_agent, "torrent-client");
//    //sessionSettings.set_str(lt::settings_pack::announce_ip, "127.0.0.1");
//    sessionSettings.set_bool(lt::settings_pack::enable_dht, false);
//    //sessionSettings.set_bool(lt::settings_pack::broadcast_lsd, true);
//    //essionSettings.set_bool(lt::settings_pack::upnp_ignore_nonrouters, false);
//
//
//
//    //sessionSettings.set_str(lt::settings_pack::dht_bootstrap_nodes, "127.0.0.1:6900,127.0.0.1:6800");
////    sessionSettings.set_bool(lt::settings_pack::enable_incoming_tcp, true);
////    sessionSettings.set_bool(lt::settings_pack::enable_outgoing_tcp, true);
////    sessionSettings.set_bool(lt::settings_pack::enable_incoming_utp, false);
////    sessionSettings.set_bool(lt::settings_pack::enable_outgoing_utp, false);
//    sessionSettings.set_int(lt::settings_pack::mixed_mode_algorithm, lt::settings_pack::bandwidth_mixed_algo_t::prefer_tcp);
//
//
//    lt::session s;
////    std::pair<std::string, int> node("127.0.0.1", 6900);
////    s.add_dht_node(node);
//    s.apply_settings(sessionSettings);
//    s.set_alert_notify([&s]()
//    {
//        std::vector<lt::alert*> alerts;
//
//        s.pop_alerts(&alerts);
//
//        for (int i = 0; i < alerts.size(); i++)
//        {
//            std::cout << "alert msg: " << alerts[i]->message() << std::endl;
//            std::cout << "alert msg: " << alerts[i]->what() << std::endl;
//        }
//    });
//
////    std::pair<std::string, int> node("127.0.0.1", 6900);
////    s.add_dht_node(node);
//
//    std::cout << "listen_port: " << s.listen_port() << std::endl;
//
//    lt::torrent_handle th = s.add_torrent(p);
//
//
//    std::vector<lt::peer_info> peerInfo;
//
//    lt::tcp::endpoint endpoint;
//    //endpoint.
//    endpoint.address(boost::asio::ip::make_address("127.0.0.1"));
//    endpoint.port(6900);
//
//    std::cout << "is valid: " << th.is_valid() << std::endl;
//
//    th.get_peer_info(peerInfo);
//
//    lt::peer_source_flags_t flags = lt::peer_source_flags_t();
//
//    //th.connect_peer(endpoint, flags);
//
////    std::vector<lt::peer_list_entry> peerList;
////
////    th.get_full_peer_list(peerList);
////
////    for(const lt::peer_list_entry& pe : peerList)
////    {
////        std::cout << "peer address: " << pe.ip.address().to_string() << std::endl;
////        std::cout << "peer port: " << pe.ip.port() << std::endl;
////        std::cout << "peer protocol: " << pe.ip.protocol().protocol() << std::endl;
////    }
//
//    for(const lt::peer_info& pi : peerInfo)
//    {
//        std::cout << "client: " << pi.client << std::endl;
//        std::cout << "address: " << pi.ip.address().to_string() << std::endl;
//        std::cout << "port: " << pi.ip.port() << std::endl;
//        std::cout << "peer id: " << pi.pid.to_string() << std::endl;
//    }
//
//    // wait for the user to end
//    char a;
//    int ret = std::scanf("%c\n", &a);
//    (void)ret; // ignore
//    return 0;
//}
