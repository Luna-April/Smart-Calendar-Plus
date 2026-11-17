#!/usr/bin/env python3
"""Push ChatGPT Plus 5h/weekly quota from Codex CLI auth to Smart Calendar.

The usage endpoint is the one Codex CLI uses; it is not a documented public
API and may change. The access token stays on this computer and is never sent
to the ESP32.

Usage:
    python3 tools/codex_quota_push.py --once
    python3 tools/codex_quota_push.py --esp32 http://<device-ip> --interval 300
"""

import argparse
import datetime as dt
import json
import os
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

DEFAULT_AUTH = os.path.expanduser("~/.codex/auth.json")
DEFAULT_ESP32 = os.environ.get("SMART_CALENDAR_URL")
USAGE_URL = "https://chatgpt.com/backend-api/wham/usage"
USER_AGENT = "codex-cli/0.153.4"
WINDOW_5H = 18000
WINDOW_WEEK = 604800


def load_auth(path):
    with open(path, encoding="utf-8") as f:
        data = json.load(f)
    tokens = data.get("tokens") or {}
    token = tokens.get("access_token")
    account = tokens.get("account_id")
    if not token or not account:
        raise RuntimeError(
            "Codex auth file is missing access_token/account_id; run `codex login`."
        )
    return token, account


def fetch_usage(token, account, timeout):
    req = urllib.request.Request(
        USAGE_URL,
        headers={
            "Authorization": "Bearer " + token,
            "ChatGPT-Account-Id": account,
            "User-Agent": USER_AGENT,
            "Accept": "application/json",
        },
    )
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return json.loads(resp.read().decode("utf-8"))


def pick_window(rate_limit, seconds):
    for name in ("primary_window", "secondary_window"):
        window = rate_limit.get(name) or {}
        try:
            if int(window.get("limit_window_seconds", 0)) == seconds:
                return window
        except (TypeError, ValueError):
            continue
    return None


def remaining_percent(window):
    if not window:
        return None
    try:
        used = float(window.get("used_percent", 0))
    except (TypeError, ValueError):
        used = 0.0
    return max(0, min(100, int(round(100 - used))))


def format_reset(window, weekly):
    if not window:
        return "--"
    reset_at = window.get("reset_at")
    if reset_at is None:
        return "--"
    try:
        reset_dt = dt.datetime.fromtimestamp(int(reset_at))
    except (TypeError, ValueError, OSError):
        return "--"
    return reset_dt.strftime("%a %H:%M" if weekly else "%H:%M")


def push_to_esp32(base_url, fields, timeout):
    body = urllib.parse.urlencode(fields).encode("utf-8")
    req = urllib.request.Request(
        base_url.rstrip("/") + "/quota",
        data=body,
        method="POST",
        headers={"Content-Type": "application/x-www-form-urlencoded"},
    )
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return resp.status, resp.read(100).decode("utf-8", "replace")


def update_once(args):
    token, account = load_auth(args.auth)
    usage = fetch_usage(token, account, args.timeout)
    plan = str(usage.get("plan_type") or "plus")
    rate_limit = usage.get("rate_limit") or {}
    window_5h = pick_window(rate_limit, WINDOW_5H)
    window_week = pick_window(rate_limit, WINDOW_WEEK)
    if window_5h is None or window_week is None:
        raise RuntimeError("usage response does not contain 5h/weekly windows")

    now = dt.datetime.now()
    fields = {
        "plan": plan,
        "h5": remaining_percent(window_5h),
        "h5r": format_reset(window_5h, weekly=False),
        "wk": remaining_percent(window_week),
        "wkr": format_reset(window_week, weekly=True),
        "upd": now.strftime("%H:%M"),
        "upd_ts": int(time.time()),
    }
    status, body = push_to_esp32(args.esp32, fields, args.timeout)
    print(
        f"[{fields['upd']}] plan={plan} "
        f"5h={fields['h5']}% ({fields['h5r']}) "
        f"wk={fields['wk']}% ({fields['wkr']}) -> ESP32 {status}"
    )
    if status != 200:
        raise RuntimeError(f"ESP32 returned {status}: {body}")
    return fields


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--esp32",
        default=DEFAULT_ESP32,
        help="ESP32 base URL, e.g. http://<device-ip>",
    )
    parser.add_argument(
        "--auth", default=DEFAULT_AUTH, help="Codex CLI auth.json path"
    )
    parser.add_argument(
        "--interval", type=int, default=300, help="seconds between pushes"
    )
    parser.add_argument(
        "--timeout", type=float, default=20, help="HTTP timeout in seconds"
    )
    parser.add_argument("--once", action="store_true", help="push once and exit")
    args = parser.parse_args()

    if not args.esp32:
        print(
            "Set SMART_CALENDAR_URL or pass --esp32 http://<device-ip>.",
            file=sys.stderr,
        )
        return 1

    while True:
        failed = False
        try:
            update_once(args)
        except urllib.error.HTTPError as exc:
            failed = True
            if exc.code in (401, 403):
                print(
                    "Codex token expired/invalid; run `codex login` "
                    "or use Codex CLI once.",
                    file=sys.stderr,
                )
            else:
                print(f"HTTP error {exc.code}: {exc.reason}", file=sys.stderr)
        except Exception as exc:
            failed = True
            print(f"error: {exc}", file=sys.stderr)
        if args.once:
            return 1 if failed else 0
        time.sleep(30 if failed else max(30, args.interval))


if __name__ == "__main__":
    sys.exit(main())
