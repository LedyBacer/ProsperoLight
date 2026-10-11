# ps5-native-app-boilerplate - ProsperoLight: the list forwarder tools read.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import json
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

PROGRAM = r"""
#include "forwarder_catalog.hpp"
#include <cassert>
using namespace prosperolight;
extern "C" uint16_t moonlight_config_host_port(const moonlight_config_host_t *host)
{
    return host->http_port ? static_cast<uint16_t>(host->http_port) : 47989;
}
static moonlight_backend_app_t app(int id, const char *name)
{
    moonlight_backend_app_t value{};
    value.id = id;
    std::snprintf(value.name, sizeof(value.name), "%s", name);
    return value;
}
int main(int, char **argv)
{
    const unsigned char png[] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n', 1, 2, 3};
    std::vector<moonlight_backend_app_t> apps = {app(1298491830, "Desktop"),
                                                 app(7, "Quote \" and \\ slash\tTab")};
    // Off until it has a folder: nothing is written anywhere.
    assert(!forwarder_catalog::publish_pc("Gaming-PC", "192.168.1.50", 47989, apps));
    assert(!forwarder_catalog::save_poster("192.168.1.50", 47989, 7, png, sizeof(png)));

    forwarder_catalog::set_root(argv[1]);
    assert(forwarder_catalog::pc_key("192.168.1.50", 47989) == "192.168.1.50_47989");
    assert(forwarder_catalog::pc_key("../a b/c", 1) == ".._a_b_c_1");
    assert(forwarder_catalog::publish_pc("Gaming-PC", "192.168.1.50", 47989, apps));
    assert(!forwarder_catalog::publish_pc("Gaming-PC", "192.168.1.50", 47989, apps)); // unchanged
    apps.push_back(app(12, "2048"));
    assert(forwarder_catalog::publish_pc("Gaming-PC", "192.168.1.50", 47989, apps));
    assert(forwarder_catalog::publish_pc("", "192.168.1.60", 48989, apps)); // named by its address
    assert(!forwarder_catalog::publish_pc("Empty", "192.168.1.70", 47989, {})); // no list: no file

    assert(!forwarder_catalog::has_poster("192.168.1.50", 47989, 7));
    assert(forwarder_catalog::save_poster("192.168.1.50", 47989, 7, png, sizeof(png)));
    assert(forwarder_catalog::has_poster("192.168.1.50", 47989, 7));
    assert(!forwarder_catalog::save_poster("192.168.1.50", 47989, 8, png + 1, sizeof(png) - 1));
    assert(forwarder_catalog::save_poster("192.168.1.60", 48989, 7, png, sizeof(png)));
    assert(forwarder_catalog::save_poster("192.168.1.80", 47989, 7, png, sizeof(png)));
    assert(forwarder_catalog::publish_pc("Gone", "192.168.1.80", 47989, apps));

    // Only the first PC is still saved: the other two go, files and box art.
    moonlight_config_t config{};
    config.host_count = 1;
    std::snprintf(config.hosts[0].address, sizeof(config.hosts[0].address), "192.168.1.50");
    forwarder_catalog::prune(config);
    assert(forwarder_catalog::has_poster("192.168.1.50", 47989, 7));
    assert(!forwarder_catalog::has_poster("192.168.1.60", 48989, 7));
    assert(!forwarder_catalog::has_poster("192.168.1.80", 47989, 7));
    return 0;
}
"""


class ForwarderCatalogTests(unittest.TestCase):
    def test_files_a_forwarder_tool_reads(self):
        compiler = shutil.which("clang++") or shutil.which("g++")
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            catalog = folder / "forwarder"
            (folder / "check.cpp").write_text(PROGRAM)
            subprocess.run([compiler, "-std=c++20", "-I" + str(ROOT / "include"),
                            str(folder / "check.cpp"), "-o", str(folder / "check")], check=True)
            subprocess.run([str(folder / "check"), str(catalog)], check=True)
            self.assertEqual(sorted(p.name for p in (catalog / "pcs").iterdir()),
                             ["192.168.1.50_47989.json"])
            self.assertEqual(sorted(p.name for p in (catalog / "posters").iterdir()),
                             ["192.168.1.50_47989"])
            pc = json.loads((catalog / "pcs/192.168.1.50_47989.json").read_text())
            self.assertEqual(pc["format"], "prosperolight-forwarder-catalog/1")
            self.assertEqual((pc["key"], pc["name"], pc["address"], pc["port"]),
                             ("192.168.1.50_47989", "Gaming-PC", "192.168.1.50", 47989))
            self.assertEqual(pc["apps"], [{"id": 1298491830, "name": "Desktop"},
                                          {"id": 7, "name": "Quote \" and \\ slash\tTab"},
                                          {"id": 12, "name": "2048"}])
            self.assertEqual((catalog / "posters/192.168.1.50_47989/7.png").stat().st_size, 11)

    def test_the_launcher_feeds_it(self):
        model = (ROOT / "src/launcher/launcher_model.cpp").read_text()
        storage = (ROOT / "src/app_storage.cpp").read_text()
        self.assertIn("forwarder_catalog::set_root(", storage)
        self.assertIn("forwarder_catalog::prune(config_);", model)
        self.assertIn("forwarder_catalog::remove_pc(host->address", model)
        self.assertIn("forwarder_catalog::save_poster(job.host, job.port, job.app_id", model)
        self.assertEqual(model.count("PublishCatalog(job.host, job.port,"), 2)
        # Box art for the catalog waits behind everything the player asked for.
        order = model.split("bool Model::StartNext()")[1]
        self.assertLess(order.index("artwork_wanted_.empty()"), order.index("catalog_wanted_.empty()"))
        self.assertLess(order.index("catalog_wanted_.empty()"), order.index("update_check_ &&"))


if __name__ == "__main__":
    unittest.main()
