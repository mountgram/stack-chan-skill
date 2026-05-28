#!/usr/bin/env bash
set -euo pipefail

app_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$app_dir"

while [[ "$repo_root" != "/" && ! -d "$repo_root/vendor/StackChan/firmware/main/apps/app_launcher" ]]; do
  repo_root="$(dirname "$repo_root")"
done

if [[ "$repo_root" == "/" ]]; then
  echo "Could not find vendor/StackChan/firmware/main/apps/app_launcher above $app_dir" >&2
  exit 1
fi

launcher_cpp="$repo_root/vendor/StackChan/firmware/main/apps/app_launcher/app_launcher.cpp"
launcher_h="$repo_root/vendor/StackChan/firmware/main/apps/app_launcher/app_launcher.h"

if [[ ! -f "$launcher_cpp" || ! -f "$launcher_h" ]]; then
  echo "AppLauncher source files not found under $repo_root/vendor/StackChan/firmware/main/apps/app_launcher" >&2
  exit 1
fi

if grep -q 'REMOTE_AGENT_BOOT_APP_NAME' "$launcher_cpp"; then
  if grep -q 'app.name' "$launcher_cpp"; then
    python3 - "$launcher_cpp" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
text = path.read_text()
text = text.replace('app.name && strcmp(app.name, REMOTE_AGENT_BOOT_APP_NAME) == 0',
                    'app.info.name == REMOTE_AGENT_BOOT_APP_NAME')
path.write_text(text)
PY
    echo "Updated existing REMOTE.AGENT boot patch"
    exit 0
  fi

  echo "REMOTE.AGENT boot patch already present"
  exit 0
fi

python3 - "$launcher_cpp" "$launcher_h" <<'PY'
from pathlib import Path
import sys

cpp_path = Path(sys.argv[1])
h_path = Path(sys.argv[2])

cpp = cpp_path.read_text()
h = h_path.read_text()

cpp = cpp.replace(
    'using namespace mooncake;\n',
    'using namespace mooncake;\n\nstatic const char* REMOTE_AGENT_BOOT_APP_NAME = "REMOTE.AGENT";\n',
)

cpp = cpp.replace(
    '    } else {\n        create_launcher_view();\n    }\n',
    '    } else {\n        create_launcher_view();\n        openRemoteAgentOnBoot();\n    }\n',
    1,
)

cpp = cpp.replace(
    '            _startup_checked = true;\n            create_launcher_view();\n',
    '            _startup_checked = true;\n            create_launcher_view();\n            openRemoteAgentOnBoot();\n',
    1,
)

insert_after = '''void AppLauncher::create_launcher_view()\n{\n    _view = std::make_unique<view::LauncherView>();\n    _view->init(getAppProps());\n    _view->onAppClicked = [&](int appID) {\n        mclog::tagInfo(getAppInfo().name, "handle open app, app id: {}", appID);\n        openApp(appID);\n    };\n}\n'''

insert = insert_after + '''\nvoid AppLauncher::openRemoteAgentOnBoot()\n{\n    if (_remote_agent_boot_opened) {\n        return;\n    }\n    _remote_agent_boot_opened = true;\n\n    for (const auto& app : getAppProps()) {\n        if (app.info.name == REMOTE_AGENT_BOOT_APP_NAME) {\n            mclog::tagInfo(getAppInfo().name, "boot opening {} app id: {}", REMOTE_AGENT_BOOT_APP_NAME, app.appID);\n            openApp(app.appID);\n            return;\n        }\n    }\n\n    mclog::tagWarn(getAppInfo().name, "{} app not found; staying in launcher", REMOTE_AGENT_BOOT_APP_NAME);\n}\n'''

cpp = cpp.replace(insert_after, insert, 1)

h = h.replace(
    '    bool _startup_checked           = false;\n',
    '    bool _startup_checked           = false;\n    bool _remote_agent_boot_opened = false;\n',
    1,
)

h = h.replace(
    '    void create_launcher_view();\n    void screensaver_update();\n',
    '    void create_launcher_view();\n    void openRemoteAgentOnBoot();\n    void screensaver_update();\n',
    1,
)

cpp_path.write_text(cpp)
h_path.write_text(h)
PY

echo "Patched AppLauncher to boot into REMOTE.AGENT while keeping launcher/home available"
