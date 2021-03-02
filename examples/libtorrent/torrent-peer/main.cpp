#include <iostream>
#include "libtorrent/entry.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/create_torrent.hpp"
#include "libtorrent/torrent_flags.hpp"
#include <libtorrent/extensions/ut_metadata.hpp>
#include <libtorrent/extensions/ut_pex.hpp>

int main(int argc, char *argv[])
{
    lt::add_torrent_params p;
    p.seeding_time = 3600;
    p.flags &= ~lt::torrent_flags::paused;
    p.flags &= ~lt::torrent_flags::auto_managed;
    p.storage_mode = lt::storage_mode_sparse;

    p.save_path = "./files";
    std::string fileName = "files/bc.log.torrent";
    p.ti = std::make_shared<lt::torrent_info>(fileName);

    std::cout << "current info_hash: " << p.ti->info_hash().to_string().c_str() << std::endl;

    lt::settings_pack sessionSettings;
    sessionSettings.set_int(lt::settings_pack::alert_mask, lt::alert_category::all);
    sessionSettings.set_int(lt::settings_pack::inactivity_timeout, 10);
    //sessionSettings.set_str(lt::settings_pack::listen_interfaces, "10.0.0.14:6900");
    sessionSettings.set_bool(lt::settings_pack::auto_manage_prefer_seeds, true);
    sessionSettings.set_str(lt::settings_pack::handshake_client_version, "torrent-peer");
    sessionSettings.set_str(lt::settings_pack::peer_fingerprint, "torrent-peer");
    sessionSettings.set_str(lt::settings_pack::user_agent, "torrent-peer");
    sessionSettings.set_str(lt::settings_pack::announce_ip, "10.0.0.14");
    //sessionSettings.set_str(lt::settings_pack::dht_bootstrap_nodes, "10.0.0.15:6800");
    sessionSettings.set_str(lt::settings_pack::dht_bootstrap_nodes, "");
    sessionSettings.set_bool(lt::settings_pack::enable_dht, true);
    sessionSettings.set_bool(lt::settings_pack::enable_lsd, true);
    sessionSettings.set_bool(lt::settings_pack::allow_multiple_connections_per_ip, true);
    sessionSettings.set_bool(lt::settings_pack::dht_restrict_routing_ips, false);

    lt::session s;
    s.apply_settings(sessionSettings);
    s.set_alert_notify([&s]()
    {
        std::vector<lt::alert *> alerts;

        s.pop_alerts(&alerts);

        for (auto & alert : alerts)
        {
            std::cout << "alert what: " << " alert msg: " << alert->message() << std::endl;
        }
    });

    std::cout << "listen_port: " << s.listen_port() << std::endl;
    std::cout << "is_dht_running: " << s.is_dht_running() << std::endl;
    std::cout << "is_listening: " << s.is_listening() << std::endl;

    s.add_extension(&lt::create_ut_metadata_plugin);
    s.add_extension(&lt::create_ut_pex_plugin);
    //s.resume();

    lt::torrent_handle th = s.add_torrent(p);
    //th.set_flags(lt::torrent_flags::seed_mode & lt::torrent_flags::super_seeding & lt::torrent_flags::auto_managed);

    lt::torrent_plugin tp;
    //th.resume();

    std::cout << "th status. total_wanted: " << th.status().total_wanted << std::endl;
    std::cout << "th status. is_seeding: " << th.status().is_seeding << std::endl;

    std::vector<lt::peer_info> peerInfo;

    for (const lt::peer_info &pi : peerInfo) {
        std::cout << "client: " << pi.client << std::endl;
        std::cout << "address: " << pi.ip.address().to_string() << std::endl;
        std::cout << "port: " << pi.ip.port() << std::endl;
        std::cout << "peer id: " << pi.pid.to_string() << std::endl;
    }

    // wait for the user to end
    char a;
    int ret = std::scanf("%c\n", &a);
    (void) ret; // ignore
    return 0;
}
