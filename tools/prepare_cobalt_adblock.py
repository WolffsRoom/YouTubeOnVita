#!/usr/bin/env python3
"""Patch an exact Cobalt 25.lts.50 checkout with an opt-in YouTube TV ad filter.

This is NOT the default upstream runtime. Uses existing native addon
configuration to decide at *page startup* whether to inject the filter.
A full application restart is required to apply a changed toggle.
"""
from pathlib import Path
import sys

def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: prepare_cobalt_adblock.py COBALT_SOURCE_ROOT")
    root = Path(__file__).resolve().parent
    cpp = Path(sys.argv[1]) / "cobalt/browser/web_module.cc"
    if not cpp.is_file():
        raise SystemExit("Missing Cobalt browser source")
    js = (root / "cobalt_adblock_preload.js").read_text(encoding="utf-8")
    anchor = (
        "  web_context_->global_environment()->CreateGlobalObject(\n"
        "      window_, web_context_->environment_settings());\n"
    )
    header_anchor = '#include "cobalt/browser/web_module.h"\n'
    marker = "// YTV_OPTIONAL_ADBLOCK_PRELOAD"
    data = cpp.read_text(encoding="utf-8")
    if marker in data:
        raise SystemExit("Already patched. Use a clean Cobalt checkout.")
    if data.count(anchor) != 1 or data.count(header_anchor) != 1:
        raise SystemExit("Unexpected Cobalt revision; patch refused")
    if ')YTVADBLOCK"' in js:
        raise SystemExit("Invalid JS raw-string boundary")
    # Vita Starboard's already-supported file API is used; the Cobalt binary
    # does not import any new custom/private symbols or read a different app.
    includes = '#include <cstring>\n#include "starboard/file.h"\n'
    patch = """
  // YTV_OPTIONAL_ADBLOCK_PRELOAD
  // No filtering unless the user enabled AdBlock in native settings.
  // The native menu's DNS filter updates immediately; this JS layer reads
  // addons.cfg when the TV document is created, so toggling it needs a restart.
  if (data.initial_url.SchemeIs("https") &&
      (data.initial_url.host() == "www.youtube.com" ||
       data.initial_url.host() == "youtube.com") &&
      (data.initial_url.path() == "/tv" ||
       data.initial_url.path().find("/tv/") == 0)) {
    bool enable_ad_filter = false;
    SbFileError file_error = kSbFileOk;
    SbFile ad_cfg = SbFileOpen("ux0:data/youtubevita/addons.cfg",
                             kSbFileOpenOnly | kSbFileRead,
                             nullptr, &file_error);
    if (ad_cfg != kSbFileInvalid) {
      char config[1024] = {};
      const int n = SbFileRead(ad_cfg, config, sizeof(config) - 1);
      SbFileClose(ad_cfg);
      if (n > 0) {
        config[n] = 0;
        // Require an exact, newline-delimited adblock=1 entry; avoid
        // substring matches such as disabled_adblock or adblock=10.
        const char* entry = std::strstr(config, "adblock=");
        while (entry != nullptr) {
          if ((entry == config || entry[-1] == '\\n') &&
              entry[8] == '1' &&
              (entry[9] == '\\n' || entry[9] == '\\r' ||
               entry[9] == '\\0')) {
            enable_ad_filter = true;
            break;
          }
          entry = std::strstr(entry + 8, "adblock=");
        }
      }
    }
    if (enable_ad_filter) {
      const char kYtvAdFilter[] = R"YTVADBLOCK(
""" + js + """
)YTVADBLOCK";
      bool preload_ok = false;
      web_context_->script_runner()->Execute(
          kYtvAdFilter, base::SourceLocation("ytv-adblock-preload.js", 1, 1),
          false, &preload_ok);
      if (!preload_ok) {
        LOG(WARNING) << "Optional YouTube TV ad filter preload failed";
      }
    }
  }
"""
    data = data.replace(header_anchor, header_anchor + includes, 1)
    data = data.replace(anchor, anchor + patch, 1)
    cpp.write_text(data, encoding="utf-8")
    print("Opt-in source patch applied to Cobalt web_module.cc; no runtime compiled.")

if __name__ == "__main__":
    main()
