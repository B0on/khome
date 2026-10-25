# 実機調査: ブラウザ / 電源・スリープ / フレームワーク / 既存環境

- 調査日: 2026-10-23（端末時計は UTC。JST では同日 12:xx）
- 接続: `ssh -o BatchMode=yes kindle` → `root@<kindle-ip>`、`Linux kindle 3.0.35-lab126 #2 PREEMPT Tue Mar 2 17:21:29 UTC 2021 armv7l`
- 方針: **読み取りのみ**。`lipc-set-prop` / `mntroot rw` / `stop`・`initctl stop` / `kill` 系は一切実行していない。端末への書き込みもしていない（`/tmp/survey/` も未使用）。
- 認証系ファイル（`/var/local/token` など）と `/var/local/mesquite/browser/cookieJar` は開いていない。ブックマーク DB 内の Cloudflare 署名付き URL のトークンは本稿では伏せている。

---

## 0. 最初に: この個体は Paperwhite 4 ではない（重要）

複数の独立した情報源が **Paperwhite 2（Pinot / Wario ボード）** を示す。

```text
$ cat /etc/version.txt
System Software Version: 001-juno_12020101_pinot_wario-372058

$ cat /mnt/us/system/version.txt
Kindle 5.12.2.1.1 (372058 001)

$ cat /proc/cpuinfo
Processor : ARMv7 Processor rev 10 (v7l)
Features  : swp half thumb fastmult vfp edsp neon vfpv3
Hardware  : Freescale i.MX 6SoloLite based Wario Board
Serial    : "<redacted>"

$ /mnt/us/usbnet/bin/fbink -e
...viewWidth=758;viewHeight=1024;screenWidth=758;screenHeight=1024;...;DPI=212;BPP=8;...
...deviceName='PaperWhite 2';deviceId=90;deviceCodename='Pinot';devicePlatform='Wario';...

$ /usr/sbin/eips -i
    xres:                    758    yres:                   1024
    xres_virtual:            768    yres_virtual:           4096
    bits_per_pixel:            8    grayscale:                 1

$ free
             total       used       free     shared    buffers     cached
Mem:        254828     247628       7200          0      29628      67132
```

- **事実**: 画面は 758×1024 / 8bpp / 212dpi、RAM は約 249MB、SoC は i.MX6SoloLite、FBInk の機種判定は `PaperWhite 2 / Pinot / Wario`。
- **推測**: 「905A」は PW2 WiFi のシリアル接頭辞。PW4 は 1072×1448 / 300dpi / RAM 512MB クラス（一般知識）なので、この数値とは合わない。
- **設計への影響**: `docs/spike-kual.md` の 1072×1448 GC16 という前提は、少なくともこの `ssh kindle` の個体には当てはまらない。ランチャーを PW4 向けに作るなら、実機をこの個体で兼用しないこと（別個体なら本調査の再実行が必要）。

ELF は soft-float ABI（システムバイナリの ELF フラグが `0x5000202` = EABI5 + `0x200` soft-float）。`armel` 前提と矛盾しない。

```text
$ /mnt/us/usbnet/bin/eu-readelf -h /usr/bin/mesquite | grep Flags
  Flags:                             0x5000202
```

---

## 1. 純正ブラウザ

### 1.1 appreg に登録されたブラウザのアプリ ID

`sqlite3` は本体に存在する（`/usr/bin/sqlite3`）。テーブル名は `handlerIds`（`handlerId` ではない）。

```text
$ sqlite3 /var/local/appreg.db ".tables"
associations  handlerIds  mimetypes  extenstions  interfaces  properties

$ sqlite3 -cmd ".mode line" /var/local/appreg.db \
    "select name,value from properties where handlerId='com.lab126.browser';"
 name = command
value = /usr/bin/mesquite -l com.lab126.browser -c file:///var/local/mesquite/browser/ -j

 name = framework
value =

 name = unloadPolicy
value = unloadOnPause

$ sqlite3 -cmd ".mode line" /var/local/appreg.db \
    "select interface,contentId,defaultAssoc from associations where handlerId='com.lab126.browser';"
   interface = application
   contentId = MT:text/html
defaultAssoc = true
```

- **アプリ ID は `com.lab126.browser`**。実体は Java booklet ではなく **mesquite（WAF）アプリ**で、`/var/local/mesquite/browser/` を `-c` で読む。
- `handlerIds` には `com.lab126.webview`（別アプリ、後述）も登録されている。
- `unloadPolicy=unloadOnPause` なので、背面に回るとアンロードされ得る。
- `MT:text/html` の既定ハンドラなので、`htm`/`html` を開く既定アプリでもある。

```text
$ sqlite3 -cmd ".mode list" -cmd ".separator |" /var/local/appreg.db \
    "select * from mimetypes where ext in ('html','htm','txt','pdf');"
htm|MT:text/html
html|MT:text/html
pdf|MT:application/pdf
txt|MT:text/plain
```

`/usr/bin/browser` や `/usr/bin/*browser*` は存在しない。ブラウザ専用の jar も見つからない（`/opt/amazon/ebook/booklet/*.jar` に browser 系なし）。

```text
$ ls /usr/bin/*browser*
ls: /usr/bin/*browser*: No such file or directory
```

ブラウザ側の設定:

```text
$ grep -nE 'id=|version' /var/local/mesquite/browser/config.xml | head
4:    id="com.lab126.browser"
5:    version="First Draft"
```

### 1.2 URL を渡す方法の手がかり

**ブラウザ（WAF アプリ）側の受け口**

`/var/local/mesquite/browser/javascripts/browser.js` の `onGo(state)`:

`onGo(state)` は受け取った `state` を名前と値の組に分解し、`state.test` があればテストを実行、`state.view` があれば `b.navigator.go(state.view)` でその URL へ遷移する。どちらも無ければ保存済みの `lastUrl`、それも無ければブックマーク一覧を開く。つまり URL を渡すキーは `view` で、`url=` ではない。

`object.fromNameValueString` は `&` 区切り・`=` 区切りで、値を `decodeURIComponent` する（`/var/local/mesquite/shared/javascripts/object.js`）。
`applicationManager.js` のコメントにも、state は `name1=value1 & name2=value2` 形式の文字列だと書かれている。
つまり **`state` は `?` の後ろのクエリ文字列**で、ブラウザが読むキーは **`view`**（と `test` / `disableJS`）。`url=` はブラウザのキーではない。

`navigator.go` が受け付けるプロトコル（`shared/javascripts/navigator.js` の `checkProtocol`）:

```js
if (a.protocol === 'http:' ||
    a.protocol === 'https:' ||
    a.href.search(/file:\/\/\/mnt\/us/i) === 0) {
    isValid = true;
}
```

→ ローカルファイルは `file:///mnt/us/...` のみ許可される。

**Amazon 自身のアプリ URI 形式**

`/var/local/mesquite/shared/javascripts/commonUtils.js`:

```js
var APP_URI = "app://$APP_NAME?$QUERY_STRING";
...
queryString = encodeURIComponent(JSON.stringify(appParams));
```

`/usr/lib/libappmgr.so.1.0` の文字列にも両形式がある:

```text
app://%s/?%s
app://%s
```

`appmgrd` の LIPC プロパティ（`lipc-probe -v com.lab126.appmgrd`）:

```text
w Str  start           # 書き込み専用
w Int  startdefault
w Str  stop
w Str  savecontext
w Str  load / goresult / loadresult / runresult / pauseresult / unloadresult
r Int  activeAppPid    [4772]
r Str  activeApp       [com.lab126.booklet.home]
```

```text
$ lipc-get-prop com.lab126.appmgrd start
com.lab126.appmgrd failed to access property start (0x9 lipcErrAccessNotAllowed)
```

**5.12 での推定（実行はしていない）**

| 起動 URI | 推定 | 根拠 |
|---|---|---|
| `app://com.lab126.browser?view=<URLエンコードした URL>` | **最有力** | `browser.js` が `state.view` を `navigator.go` に渡す。Amazon 自身の `APP_URI` が `app://$APP?$QUERY` 形式 |
| `app://com.lab126.browser/?view=<...>` | 次点（未確認） | `libappmgr.so` に `app://%s/?%s` という生成形式がある。パス付きでも `?` 以降が state になる可能性 |
| `app://com.lab126.browser?url=<...>` | **効かない見込み** | ブラウザ側に `url` キーの読み取りがない。`state.view` が undefined になり `lastUrl`／ブックマークへ落ちる |
| `app://com.lab126.webview?%7B%22url%22%3A...%7D` | 別解（chrome なし） | `com.lab126.webview` は `url` と `nextApp`/`nextAppParams` を JSON クエリで受ける（`webview.js`） |

推奨（次回、`lipc-set-prop` が許可されたときの確認手順）:

```sh
lipc-set-prop com.lab126.appmgrd start \
  'app://com.lab126.browser?view=https%3A%2F%2Fexample.com%2F'
# 確認:
lipc-get-prop com.lab126.appmgrd activeApp      # → com.lab126.browser
grep -a 'navigating to provided URL' /var/log/messages | tail
```

注意点:

- `&` と `=` は state の区切り文字なので、**URL 全体を `encodeURIComponent` する**のが安全。`view` の値だけを渡すと `?a=1&b=2` のような URL が壊れる。
- 起動時にブラウザが前面でなければ、`appmgrd` が `load` → `go` の順で state を渡す（ログで確認できる `app_state_change` の流れ）。
- **推測**: 既にブラウザが起動中（paused）の場合も `go` が再送され `onGo` が走るはずだが、`unloadOnPause` のため一度終了している可能性がある。実測が必要。
- `test=<script URL>` を渡すと `tester.js` がその JS をブラウザの `document.head` に注入し、`window.kindleApp` に sandbox を露出する。自動化の穴になり得るが、**未検証**（`file:///mnt/us/...` を読ませる想定）。設計で使うなら別途検証すること。

### 1.3 ブックマークの保存場所と形式

`find` ではデータファイルは出てこない（JS のみ）:

```text
$ find /var/local /mnt/us -iname '*bookmark*' 2>/dev/null
/var/local/mesquite/shared/javascripts/bookmarks.js
/var/local/mesquite/shared/js/bookmarks.js
/var/local/mesquite/browser/javascripts/bookmarksView.js
```

`/var/local/java/prefs/browser/` は存在しない。`/var/local` 直下の `*.db` は次の 8 個で、ブラウザ用 DB はない。

```text
$ ls -1 /var/local/*.db
/var/local/KWISClient.db
/var/local/WishList.db
/var/local/appreg.db
/var/local/bw.db
/var/local/cc.db
/var/local/dcm.db
/var/local/grok.db
/var/local/wsync.db
# ほかに /var/local/reg/prereg_*.db が 57 個（KAF のアプリ登録 DB）
```

**実体は WebKit の localStorage**（`storage.js` が `localStorage` を使う）。ブラウザの sandbox は `/mnt/us` 側:

```text
$ ls -la /mnt/us/.active_content_sandbox/browser/resource/LocalStorage/
-rwxrwxrwx 1 root root 24576 Oct 22 18:29 file__0.localstorage

$ file /mnt/us/.active_content_sandbox/browser/resource/LocalStorage/file__0.localstorage
... SQLite 3.x database, last written using SQLite version 3026000, ... UTF-16 little endian ...

$ hexdump -C .../file__0.localstorage | head -3
00000000  53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00  |SQLite format 3.|
00000010  10 00 01 01 00 40 20 20  00 00 00 0d 00 00 00 06  |.....@  ........|
00000020  00 00 00 05 00 00 00 02  00 00 00 01 00 00 00 04  |................|

$ sqlite3 .../file__0.localstorage ".schema"
CREATE TABLE ItemTable (key TEXT UNIQUE ON CONFLICT REPLACE, value TEXT NOT NULL ON CONFLICT FAIL);
```

JSON 本体は SQLite のページ内にあるので、ファイル先頭の hexdump には現れない。値の先頭は `substr` の hex で確認する（下記）。

キーは 4 つ:

```text
$ sqlite3 .../file__0.localstorage "select key, length(value) from ItemTable;"
bookmarks|476
settings|72
lastUrl|34
history|3116
# length() は文字数。UTF-16LE なので実バイト数は 2 倍（bookmarks=952B, settings=144B, lastUrl=68B, history=6232B）
```

値は **UTF-16LE の JSON**（SQLite の TEXT だが WebKit が UTF-16 で書く）。先頭バイトで確認:

```text
$ sqlite3 .../file__0.localstorage \
    "select hex(substr(value,1,12)) from ItemTable where key='bookmarks';"
5B007B0022006E0061006D00650022003A00220041006D00
# 5B 00 = '[' , 7B 00 = '{' , 22 00 = '"' ... → "[{\"name\":\"Am...
```

デコード結果:

- `bookmarks`（9 件、すべて初期値。ユーザー追加分はなし）

| name | url |
|---|---|
| Amazon.co.jp | https://www.amazon.co.jp/ |
| mixi (ミクシィ） | https://mixi.jp/ |
| Yahoo! Japan | https://www.yahoo.co.jp/ |
| Google | https://www.google.co.jp/ |
| Outlook | https://www.outlook.com/ |
| Wikipedia | https://ja.wikipedia.org/ |
| Twitter | https://twitter.com/ |
| はてな | https://www.hatena.ne.jp/ |
| Facebook | https://ja-jp.facebook.com/ |

- `settings`: `{"javascriptDisabled":false,"imagesDisabled":false,"orientation":"auto"}`
- `lastUrl`: `"https://example.com/lite"`（最終閲覧 URL）
- `history`: 5 件。うち 2 件は Cloudflare Access のログイン URL で、`kid=` / `meta=` に署名付きトークンを含む（**本稿では値とクエリを伏せる**）。タイトルは `Sign in ・ Cloudflare Access`（2 件）と `Example Canvas`（3 件）。

**事実**: ブラウザのブックマークは `/mnt/us/.active_content_sandbox/browser/resource/LocalStorage/file__0.localstorage` の `ItemTable` に UTF-16LE JSON で入る。
**推測**: 読み取りは `sqlite3` + `iconv` で可能（例: Mac 側で `sqlite3 ... | iconv -f UTF-16LE -t UTF-8`）。ただしブラウザ起動中に書くと WebKit のメモリ上の localStorage に上書きされるため、**書き込みはブラウザ停止中のみ**。今回は読み取りだけ行い、DB の mtime は変わっていない（`2026-10-22 18:29:22` のまま、`-journal`/`-wal` も生成されていない）。

---

## 2. 電源・スリープ・復帰の検知

### 2.1 powerd のプロパティ（`lipc-probe -v com.lab126.powerd` 全 28 件）

| r/w | 型 | 名前 | 現在値 |
|---|---|---|---|
| w | Int | touchScreenSaverTimeout | — |
| w | Int | deferSuspend | — |
| w | Int | wakeUp | — |
| r | Int | flRawIntensity | 14 |
| w | Int | rtcWakeup | — |
| r | Str | **state** | **active** |
| w | Int | suspendGrace | — |
| r | Str | battStateInfo | `{"battInfo":[{"type":"PRIMARY","cap":92,"charging":false}],"state":"STANDALONE","aggr_batt":92,"usb":false}` |
| w | Int | deferRestart | — |
| w | Str | shutdownAlertResponse | — |
| r | Int | battTemperature | 77 |
| w | Int | addSuspendLevels | — |
| rw | Str | logLevel | none |
| r | Int | battLevel | 92 |
| rw | Int | **preventScreenSaver** | **0** |
| rw | Str | logMask | 0x00000000 |
| w | Int | sendPendingResume | — |
| rw | Int | flAuto | 0 |
| rw | Int | flIntensity | 5 |
| r | Int | flMaxIntensity | 25 |
| w | Str | rtcWakeup2 | — |
| w | Int | abortSuspend | — |
| r | Int | flTurboBrightnessIntensity | 1 |
| w | Int | powerButton | — |
| w | Int | flStartup | — |
| r | Str | **status** | 下記 |
| r | Int | isLowTempState | 0 |
| r | Int | isCharging | 0 |

`status` の現在値:

```text
Powerd state: Active
Remaining time in this state: 69.045465
defer_suspend:0
suspend_grace:0
prevent_screen_saver:0
drive_mode_state:1
PRIMARY BATTERY
Battery Level: 92%
Last batt event at: 92%
Charging: No
Battery logging: On
```

- **事実**: `state` は現在 `active`。`status` に「次の遷移までの残り秒」が出る（上は 69 秒。ポーリングのたびに値が変わる）。
- **事実**: `screenSaverState` / `batteryLevel` / `chargingState` というプロパティは**存在しない**（`0x8 lipcErrNoSuchProperty`）。バッテリーは `battLevel` / `battStateInfo` / `isCharging` を使う。
- **推測**: `preventScreenSaver=1` でスクリーンセーバーを抑止（既存 `kindle/extensions/newspaper` が使用）。`deferSuspend` は `readyToSuspend` のカウントダウンを延ばすためのもの、`powerButton` は電源ボタンの擬似入力、と読めるが**未検証**。

**調査中の観測**: 調査の終盤（最終コマンド成功の数分後）に `ssh kindle` がタイムアウトし、`ping <kindle-ip>` も 100% ロスになった。直前の `status` が「Remaining time in this state: 49s」だったことから、**自動サスペンドで Wi-Fi が落ちた**と推測される（電源ボタンは押していない）。この個体は SSH 接続中でもアイドルでサスペンドし得る、という運用上の注意になる。

### 2.2 powerd の状態遷移とイベント

**状態機械（ログの `def:statech` から。過去ログ全体の集計）**

```text
$ ... | grep -a "def:statech:prev=" | sed -E 's/.*prev=(.*), next=(.*):State change.*/\1 -> \2/' | sort | uniq -c
  6 ACTIVE -> SCREEN SAVER
  6 SCREEN SAVER -> ACTIVE
 39 SCREEN SAVER -> READY TO SUSPEND
 39 READY TO SUSPEND -> SUSPENDED
 39 SUSPENDED -> SCREEN SAVER
```

つまり `ACTIVE → SCREEN SAVER → READY TO SUSPEND → SUSPENDED →（復帰時）SCREEN SAVER → ACTIVE`。`lipc` の `state` プロパティはこれを小文字化した値（`active`）を返すとみられる。

**実際に送られたイベント（`origin=com.lab126.powerd`、全ログ集計）**

| イベント | 回数 | 観測された fparam / 備考 |
|---|---:|---|
| `readyToSuspend` | 239 | カウントダウン。例 `7,6,2,1`（`SCREEN SAVER → READY TO SUSPEND` 中に繰り返し） |
| `battLevelChanged` | 66 | `92`, `93` |
| `wakeupFromSuspend` | 39 | `161`, `25369`（休眠秒数と推測） |
| `suspending` | 39 | `mem` |
| `t1TimerReset` | 33 | 引数なし。詳細不明 |
| `battStateInfoChanged` | 8 | JSON |
| `outOfScreenSaver` | 6 | `1`（通常復帰）, `2`（USB 接続後の復帰） |
| `goingToScreenSaver` | 6 | `2`。**全件が電源ボタン押下**（`GoingToScreensaverDueToPowerButtonPress` と同時） |
| `exitingScreenSaver` | 6 | 引数なし |
| `resuming` | 5 | 引数なし |
| `usbConfigured` / `charging` / `notCharging` | 各 4 | USB |

代表ログ（実物）:

```text
# 前の休眠から復帰した周期
261022:184431 powerd[1970]: I lipc:evts:name=wakeupFromSuspend, origin=com.lab126.powerd, fparam=161:Event sent
261022:184431 powerd[1970]: I lipc:evts:name=resuming, origin=com.lab126.powerd:Event sent
261022:184431 powerd[1970]: I lipc:evts:name=outOfScreenSaver, origin=com.lab126.powerd, fparam=1:Event sent
261022:184431 powerd[1970]: I lipc:evts:name=exitingScreenSaver, origin=com.lab126.powerd:Event sent

# 電源ボタン → スクリーンセーバー → 自動サスペンド
261022:184514 powerd[1970]: I def:pbpress:time=1792694714.021:Power button pressed
261022:184514 powerd[1970]: I def:statech:prev=ACTIVE, next=SCREEN SAVER:State change: ACTIVE -> SCREEN SAVER
261022:184514 powerd[1970]: I lipc:evts:name=goingToScreenSaver, origin=com.lab126.powerd, fparam=2:Event sent
261022:184624 powerd[1970]: I lipc:evts:name=readyToSuspend, origin=com.lab126.powerd, fparam=10:Event sent
261022:184718 powerd[1970]: I lipc:evts:name=suspending, origin=com.lab126.powerd, fparam=mem:Event sent
```

**バイナリ／設定にはあるが、今回のログでは観測できなかったイベント**

`/usr/bin/powerd` の文字列、および `/etc/lipc-daemon-events.conf` から:

| イベント | 根拠 |
|---|---|
| `userShutdown` | powerd の文字列 |
| `PowerButtonHeld` | powerd の文字列 + `/etc/lipc-daemon-events.conf:13`（`PowerButtonHeld.sh`） |
| `PowerButtonChange` | powerd の文字列 + `/etc/lipc-daemon-events.conf:14`（`PowerButtonChange.sh`） |
| `notReadyToSuspend` | powerd の文字列（`readyToSuspend` の対） |
| `aboutToRestart` | powerd の文字列 |
| `goingToActiveCoverOn` / `outOfActiveCoverOn` | powerd の文字列（Active Cover 機能） |
| `nonDtcpScreensaverOut` | powerd の文字列 |
| `suspendGrace` / `deferSuspend` / `abortSuspend` | プロパティ名。イベントかどうかは不明 |

- **事実**: 質問にあった `goingToScreenSaver` / `outOfScreenSaver` / `wakeupFromSuspend` / `readyToSuspend` は**実在し、実際に送られている**。`notReadyToSuspend` も文字列としては存在。
- **不明**: `goingToScreenSaver` の `fparam` の意味（今回の 6 件はすべて電源ボタンで `2`。タイムアウト時は別値と推測）。`outOfScreenSaver` の `1`/`2` の意味も不明（1=通常、2=USB 復帰、と読めるが未確認）。
- **推測**: スリープ直前の `readyToSuspend` は「あと N 秒で suspend」の通知で、`deferSuspend` を書くと延長できる構造と思われる（`readyToSuspend` が `7,6,2,1` と減っていくログと、`defer_suspend:0` という status 表示から）。

### 2.3 `lipc-wait-event`

存在する（`/usr/bin/lipc-wait-event`）。`--help` はなく、不正引数で usage が出る。

```text
$ lipc-wait-event --help
lipc-wait-event waits for an event to be received.
Usage: lipc-wait-event [options] [<event-source>] <event-names>
OPTIONS:
<event-names>: It is a comma separated string of events you want to listen for
    s: Max wait seconds (must be >0 and not a fraction)
    u: Max wait in microseconds (must be >0)
    m: Wait for multiple events (don't exit after one event)
    r: When combined with -u/-s and -m, restart timeout after each event
    t: Print time stamp with each output line
    l: List of event sources is read from stdin, one per line.
NOTES:
    1. You can use * as event-name.
    2. When you use *, don't forget to enclose it in quotes
    3. Notice that the output format is different when you use * ...
```

実測（3 秒タイムアウト、イベントなし）:

```text
$ t0=$(date +%s); lipc-wait-event -s 3 com.lab126.powerd goingToScreenSaver,outOfScreenSaver,wakeupFromSuspend; rc=$?; t1=$(date +%s); echo "rc=$rc elapsed=$((t1-t0))s"
rc=255 elapsed=3s
```

- **事実**: タイムアウト時は **rc=255**、出力なし。
- **事実**: 存在しないイベント名（`bogusEventXYZ`）でもエラーにならず、同じく 255 でタイムアウトする → イベント名の妥当性チェックはしない。
- **推奨の購読例**（ランチャー側）:

```sh
lipc-wait-event -m -t -s 3600 com.lab126.powerd \
  'goingToScreenSaver,outOfScreenSaver,readyToSuspend,wakeupFromSuspend,resuming,userShutdown'
```

`-m` を付けないと最初の 1 イベントで終了する。`-s` を付けないと無期限にブロックするので、常時 `timeout` と併用するのが安全。

### 2.4 電源ボタンの入力デバイス

`/proc/bus/input/devices` 全内容（2 デバイスのみ）:

```text
I: Bus=0000 Vendor=0000 Product=0000 Version=0000
N: Name="max77696-onkey"
P: Phys=max77696-onkey/input0
S: Sysfs=/devices/platform/imx-i2c.0/i2c-0/0-003c/max77696-onkey.0/input/input0
U: Uniq=
H: Handlers=kbd event0
B: PROP=0
B: EV=3
B: KEY=100000 0 0 0

I: Bus=0000 Vendor=0000 Product=0000 Version=0000
N: Name="cyttsp4_mt"
P: Phys=cyttsp4_mt.main_ttsp_core
S: Sysfs=/devices/platform/imx-i2c.1/i2c-1/1-0024/main_ttsp_core.cyttsp4_i2c_adapter/cyttsp4_mt.main_ttsp_core/input/input1
U: Uniq=
H: Handlers=event1
B: PROP=0
B: EV=f
B: KEY=6420 0 0 0 0 0 0 0 0 0 0
B: REL=0
B: ABS=2608000 0
```

**KEY_POWER を持つのは `/dev/input/event0`（max77696-onkey）**。

- `event0` の `KEY=100000 0 0 0` は 4 ワード（=128bit）を上位ワードから並べたもの。`0x100000` = 最上位ワードの bit20 → 116 = **KEY_POWER のみ**。`EV=3` は `EV_SYN|EV_KEY`。
- `event1` はタッチ。`KEY` は BTN_TOOL_FINGER(325) / BTN_TOUCH(330) / BTN_TOOL_DOUBLETAP(333) / BTN_TOOL_TRIPLETAP(334)、`ABS` は ABS_MT_SLOT(47) / ABS_MT_POSITION_X(53) / ABS_MT_POSITION_Y(54) / ABS_MT_TRACKING_ID(57)（MT protocol B）。`REL=0` なので ABS_X/Y は使わない。
- **注意**: 同梱の `evtest --query` はこの端末では**壊れている**。`KEY_POWER` でも未対応キー（`KEY_KBDILLUMUP`）でも rc=0 を返す。判定には sysfs の `capabilities/key` をデコードすること。以下は再現ログ:

```text
$ evtest --query /dev/input/event0 EV_KEY KEY_POWER; echo $?   # 0
$ evtest --query /dev/input/event0 EV_KEY KEY_KBDILLUMUP; echo $?  # 0 ← 本当は非対応
```

既存の `touch_reader` は `/dev/input/event1` を grab して使っている（`/mnt/us/newspaper/reader.log` の `touch_reader: device=/dev/input/event1 grab=1 keys=0`）。

---

## 3. フレームワークの構成

### 3.1 プロセス一覧（`ps -eo pid,ppid,args` 抜粋）

| PID | PPID | プロセス | 引数（抜粋） |
|---:|---:|---|---|
| 1894 | 1 | `powerd` | `powerd -f` |
| 1899 | 1 | `appmgrd` | `appmgrd` |
| 2541 | 1 | `lxinit` | `lxinit blanket -t splash screensaver langpicker blankwindow` |
| 2615 | 2541 | `Xorg` | `Xorg -nolisten tcp +bs -ardelay 0 -arinterval 0` |
| 3045 | 2541 | `awesome` | `awesome`（WM） |
| 3046 | 2541 | `blanket` | `blanket -t splash screensaver langpicker blankwindow` |
| 3504 | 1 | `kb` | `kb` |
| 3557 | 3539 | `pillowd` | `pillowd -f` |
| 4098 | 1 | `webreader` | `webreader` |
| 4119 | 1 | `sh` | `/bin/sh /etc/upstart/framework` |
| 4772 | 4119 | `cvm` | `/usr/java/bin/cvm -Xmx25m ...`（巨大な classpath。ホーム booklet を実行） |
| 6251 | 6229 | `mesquite` | `/usr/bin/mesquite -d com.lab126.stored`（ストアのみ。ブラウザは未起動） |
| 5759 | 1 | `kfxreader` | `kfxreader -l` |
| 5762 | 1 | `kfxview` | `kfxview` |
| 1859 | 1 | `lipc-daemon` | `lipc-daemon -f -p ... -e ...` |
| 1915 | 1 | `deviced` | `deviced -f` |
| 2024 | 1 | `volumd` | `volumd -f -d /dev/mmcblk0p4` |
| 4970 | 1 | `wpa_supplicant` | `-i wlan0 ...` |
| 6378 | 1 | `udhcpc` | `-i wlan0` |
| 2316 | 1 | `dropbear` | `/usr/bin/dropbear -P /mnt/us/usbnet/run/sshd.pid -K 15` |

- **事実**: X11 + awesome + pillowd + blanket の構成。`lxinit` が Xorg / awesome / blanket の親。
- **事実**: ブラウザ（mesquite `com.lab126.browser`）は**起動していない**。mesquite はストア用に 1 個だけ動いている。
- `cvm` はホーム booklet（Java）で、`activeApp` と一致する。

### 3.2 upstart / initctl

`initctl` は PATH にないが `/sbin/initctl` にある。

```text
$ /sbin/initctl list
...
framework start/running, process 4119
lab126 start/running
lab126_gui start/running
lab126_gui_monitor stop/waiting
lab126_gui_setup stop/waiting
x start/running, process 2541
pillow start/running, process 3539
stored start/running, process 6229
powerd start/running, process 1894
appmgrd start/running, process 1899
webreader start/running, process 4098
kb start/running, process 3504
display stop/waiting
splash stop/waiting
...
```

`/etc/upstart/` には 97 個の `.conf`。lab126_gui 関連は次のとおり:

```text
lab126.conf
lab126_gui.conf
lab126_gui_monitor.conf
lab126_gui_setup.conf
framework.conf
framework_setup.conf
kindle.conf
x.conf
x_setup.conf
x_cleanup.conf
x_monitor.conf
pillow.conf
stored.conf
powerd.conf
appmgrd.conf
webreader.conf
kb.conf
blanket_functions   # .conf ではない関数ファイル
```

`/etc/upstart/lab126_gui.conf` の先頭:

```text
start on langpicker_ready
stop on stopping x or ota-update

emits start_splash

env MAX_RESTARTS=3
env MAX_REBOOTS=2
...
```

- **事実**: `lab126_gui` は `langpicker_ready` で起動し、`stopping x` または `ota-update` で停止する。再起動回数ガード（3 回で reboot、2 回で customer service 画面）を持つ。
- カスタムの kiosk 用ジョブ（`kindle-times.conf` 等）は `/etc/upstart/` に**存在しない**。

### 3.3 appmgrd

```text
$ lipc-get-prop com.lab126.appmgrd activeApp
com.lab126.booklet.home

$ lipc-get-prop com.lab126.appmgrd activeAppPid
4772
```

`lipc-probe -v com.lab126.appmgrd`（全 22 件、値のあるもの）:

```text
w Str  loghistory
w Str  backgroundAppUnLoaded / backgroundAppLoaded
w Str  load / goresult / loadresult / runresult / pauseresult / unloadresult
w Str  savecontext / stop / defer / logallapps
w Int  startdefault / forward / backward / clearAllContext
rw Str logLevel  [info]
rw Str logMask   [0x0fff0000]
w Str  start
r Int  activeAppPid [4772]
r Str  activeApp    [com.lab126.booklet.home]
```

- `start` は**書き込み専用**（読むと `0x9 lipcErrAccessNotAllowed`）。ここに `app://...` を渡すのがアプリ起動の正規ルート。
- サードパーティ登録の実例として KUAL: `handlerIds` に `com.mobileread.ixtab.kindlelauncher`、properties は `jar=/opt/amazon/ebook/booklet/KUALBooklet.jar`, `lipcId=com.mobileread.ixtab.kindlelauncher`, `unloadPolicy=unloadOnPause`, `maxGoTime=60`, `maxPauseTime=60`, `maxUnloadTime=45`, `supportedOrientation=U`, `searchbar-mode=transient`。**事実**: WAF でない通常の Kindlelet はこのように jar + lipcId で登録できる。

### 3.4 画面

- `/usr/sbin/eips` が存在（57,008 bytes）。usage の先頭:

```text
$ /usr/sbin/eips
 For complete usage and examples, search eips in wiki page.
usage:
to print strings: eips [row] [col] [-h] string
to show image: eips -g|-b image_path [-w waveform -f -x xpos -y ypos -v]
to paint a retangle on screen: eips -d l=a,w=b,h=c [-x xx -y yy -w wf]
to clear display: eips -c
...
to print fb info: eips -i
```

- FBInk は 2 箇所:
  - `/mnt/us/usbnet/bin/fbink`（1,119,920 bytes, 2023-11-04, `v1.25.0-179-gacf31a3`）
  - `/mnt/us/libkh/bin/fbink`（116,348 bytes, 2020-12-01）
  - `/usr/bin/fbink` は `/mnt/us/usbnet/bin/fbink` への symlink。
- `fbink -e`（描画なし、情報のみ）:

```text
FBINK_VERSION='v1.25.0-179-gacf31a3';FBINK_TARGET=2;FBINK_FEATURES=0x37;
viewWidth=758;viewHeight=1024;screenWidth=758;screenHeight=1024;
viewHoriOrigin=0;viewVertOrigin=0;viewVertOffset=0;DPI=212;BPP=8;
lineLength=768;invertedGrayscale=0;FONTW=16;FONTH=16;FONTSIZE_MULT=2;
FONTNAME='IBM';glyphWidth=8;glyphHeight=8;MAXCOLS=47;MAXROWS=64;
isPerfectFit=0;FBID='mxc_epdc_fb';USER_HZ=100;penFGColor=0;penBGColor=255;
deviceName='PaperWhite 2';deviceId=90;deviceCodename='Pinot';
devicePlatform='Wario';isMTK=0;isSunxi=0;SunxiHasFBDamage=0;
SunxiForceRota=-128;isKindleLegacy=0;isKoboNonMT=0;unreliableWaitFor=0;
ntxBootRota=0;ntxRotaQuirk=0;rotationMap='{ 0, 0, 0, 0 }';
touchSwapAxes=0;touchMirrorX=0;touchMirrorY=0;isNTX16bLandscape=0;
currentRota=3;canRotate=0;canHWInvert=1;hasEclipseWfm=0;canWaitForSubmission=1;
```

- **事実**: 画面は 758×1024、8bpp、lineLength 768、`mxc_epdc_fb`、`currentRota=3`、`canRotate=0`。**FBInk の全画面描画は 758×1024 を前提にすること**。
- `eips -i` の framebuffer 情報も 758×1024 / 8bpp / grayscale=1 で一致。

---

## 4. 既存環境

### 4.1 `/mnt/us` 直下と extensions

```text
$ ls -la /mnt/us/
（抜粋。実際には .Spotlight-V100, .fseventsd, System Volume Information, kindlebreak_log.txt なども含む）
drwxrwxrwx  .active_content_sandbox
drwxrwxrwx  documents
drwxrwxrwx  extensions
drwxrwxrwx  fonts
drwxrwxrwx  libkh
drwxrwxrwx  mkk
drwxrwxrwx  mrpackages
drwxrwxrwx  newspaper
drwxrwxrwx  rp
drwxrwxrwx  system
drwxrwxrwx  usbnet
```

- `/mnt/us/koreader` は**無い**。
- `/mnt/us/extensions/`:

```text
KUAL.cfg
MRInstaller/
newspaper/
renameotabin/
usbnet/
```

- KUAL の場所:
  - ランチャー本体: `/mnt/us/documents/KUAL.kual`（0 バイトのマーカー）と `/opt/amazon/ebook/booklet/KUALBooklet.jar`（50,770 bytes）
  - 拡張: `/mnt/us/extensions/`（`KUAL.cfg` は 2020-12-01 作成、中身はコメントのみ）
  - appreg 登録: `com.mobileread.ixtab.kindlelauncher`（3.3 参照）
- 既存の自作拡張 `newspaper`（The Kindle Times）が `/mnt/us/extensions/newspaper/` に残っている（`config.xml`, `menu.json`, `bin/`, `config/`）。

### 4.2 旧キオスクの残骸

```text
$ ls -la /etc/upstart/kindle-times.conf
ls: /etc/upstart/kindle-times.conf: No such file or directory
```

- **事実**: 旧キオスクの upstart ジョブは**残っていない**（工場出荷リセット相当の状態）。`/etc/upstart/` を `newspaper` / `kindle-times` で grep しても何も出ない。
- `/mnt/us/newspaper/` の状態:

```text
$ ls -la /mnt/us/newspaper/
...
-rwxrwxrwx  latest.json       (190 bytes)
-rwxrwxrwx  reader.log        (314 bytes)
-rwxrwxrwx  server.url        (26 bytes)
-rwxrwxrwx  unkiosk           (8 bytes, 2026-10-23 02:55)
drwxrwxrwx  current/
drwxrwxrwx  previous/
```

```text
$ cat /mnt/us/newspaper/unkiosk
unkiosk
$ cat /mnt/us/newspaper/server.url
http://<server-ip>:8088
$ cat /mnt/us/newspaper/latest.json
{
  "id": "20260916-2356",
  "sha256": "4b70b2e7e975efa296fcf76ac150802726c6b5c1ab8b6731fed46852bb6d8d83",
  "generated_at": "2026-09-16T23:56:45+09:00",
  "path": "edition-20260916-2356"
}
```

- **事実**: `unkiosk` フラグが**存在する**（= キオスク解除状態）。`kiosk` / `active` フラグは無い。
- `current/` には `SHA256SUMS`, `ja/p01.png`〜 と `.kindle-times-cache`（`kindle-times-cache-v1`）が残っている。
- `reader.log` の末尾:

```text
touch_reader: device=/dev/input/event1 grab=1 keys=0
touch_reader: tap 53,74 -> exit
kindle-times: Could not download  ja/articles/static-00-p01.png
wget: can't connect to remote host (<server-ip>): Network is unreachable
kindle-times: Could not download latest.json
```

→ 前回は LAN サーバ（<server-ip>:8088）に到達できず失敗している。

### 4.3 空き容量・メモリ・CPU

```text
$ df -h
Filesystem                Size      Used Available Use% Mounted on
/dev/root               348.1M    309.5M     31.6M  91% /
tmpfs                   124.4M    100.0K    124.3M   0% /dev
tmpfs                    32.0M      1.0M     31.0M   3% /var
/dev/mmcblk0p3           62.0M     19.0M     39.7M  32% /var/local
fsp                       3.2G     64.0M      3.1G   2% /mnt/us
```

```text
$ free
             total       used       free     shared    buffers     cached
Mem:        254828     247628       7200          0      29628      67132
-/+ buffers/cache:     150868     103960
Swap:            0          0          0
```

```text
$ cat /proc/cpuinfo
Processor : ARMv7 Processor rev 10 (v7l)
BogoMIPS  : 1987.37
Features  : swp half thumb fastmult vfp edsp neon vfpv3
CPU implementer : 0x41
CPU architecture: 7
CPU variant : 0x2
CPU part  : 0xc09
CPU revision : 10
Hardware  : Freescale i.MX 6SoloLite based Wario Board
```

- **事実**: `/` は 91% 使用で残り 31.6MB しかない。`/mnt/us` は 3.1GB 空き。`/var/local` は 39.7MB 空き。
- **事実**: RAM は 249MB。buffers/cache を除いた実空きは約 101MB（`free` の `-/+ buffers/cache: free 103960` KB）だが、瞬間空きは 7MB を切ることがある。cvm が -Xmx25m で動いている。
- **事実**: i.MX6SoloLite（シングルコア、`/proc/cpuinfo` に `processor:` 行は無く `Processor:` のみ）。NEON/VFP あり。

### 4.4 時刻とタイムゾーン

```text
$ date
Fri Oct 23 03:04:58 UTC 2026
$ date -u
Fri Oct 23 03:04:58 UTC 2026

$ ls -la /etc/localtime /var/local/system/tz
lrwxrwxrwx 1 root root 20 Mar  2  2021 /etc/localtime -> /var/local/system/tz
ls: /var/local/system/tz: No such file or directory
```

- **事実**: `/etc/localtime` は `/var/local/system/tz` への symlink だが、**リンク先が存在しない**。そのため現在は **UTC**。
- **事実**: `/usr/share/zoneinfo/` には `iso3166.tab` と `zone.tab` しかなく、**tzdata 本体（Asia/Tokyo 等）が入っていない**。`TZ=Asia/Tokyo date` も `Asia` というゾーン名で UTC 相当の時刻を返す。
- タイムゾーンは `/usr/sbin/settz` が `zic -d /var/local/system` で**固定オフセットの tz ファイルを生成**する仕組み:

```text
FAKEZONEINFO="/var/local/system"
...
echo "Zone tz $(tz_off ${_arg}) - ${_tzname}" | zic -d ${FAKEZONEINFO} -
```

- **事実**: `TZ=JST-9 date` は tzdata なしで正しく JST を返す:

```text
$ TZ=JST-9 date
Fri Oct 23 12:07:58 JST 2026
```

→ ランチャーが「日本時間」を表示・判定するなら、tzdata に依存せず `TZ=JST-9`（POSIX TZ 文字列）を使うのが現実的。**推測**であり、実運用では `settz 32400`（= +9h）で `/var/local/system/tz` を作る方法もあるが、今回は書き込み禁止のため未実施。

- ロケールは `ja_JP.utf8`:

```text
$ cat /var/local/system/locale
LANG=ja_JP.utf8
LC_ALL=ja_JP.utf8
```

---

## 5. 不明点・次に確認すること

1. **起動 URI の最終確認（最重要）**
   - `app://com.lab126.browser?view=...` が実際に効くか。`lipc-set-prop` 禁止のため未実行。
   - 次回: `lipc-set-prop com.lab126.appmgrd start 'app://com.lab126.browser?view=https%3A%2F%2Fexample.com%2F'` → `activeApp` と `/var/log/messages` の `navigating to provided URL` を確認。
   - 効かなければ `app://com.lab126.browser/?view=...` も試す。両方ダメなら `com.lab126.webview`（`url` を JSON で渡す）へ切り替える。

2. **ブラウザが既に起動中／paused のときの `view` の扱い**
   - `unloadPolicy=unloadOnPause` なので、`start` のたびに再ロードされる可能性が高い。`onGo` が毎回呼ばれるかは実測が必要。

3. **`view` の URL 長・文字種の上限**
   - LIPC の文字列長や `encodeURIComponent` の扱いは未検証。長い URL（トラッキングパラメータ付き）で切れないか要確認。

4. **powerd の `fparam` の意味**
   - `goingToScreenSaver` の `2` は電源ボタン（ログの `GoingToScreensaverDueToPowerButtonPress` と一致）。タイムアウト時・USB 接続時・カバー時の値は未観測。
   - `outOfScreenSaver` の `1`/`2` も未確定（1=通常、2=USB 復帰と推測）。

5. **スリープ抑止の実効性**
   - `preventScreenSaver=1` が実際に `goingToScreenSaver` を止めるかは、既存 `newspaper` の実装に依存しており、今回の読み取りだけでは未確認。
   - `deferSuspend` の単位・最大値・`readyToSuspend` との関係も未確認。

6. **`state` プロパティの値域**
   - 実測は `active` のみ。`SCREEN SAVER` / `READY TO SUSPEND` / `SUSPENDED` に対応する小文字値は未観測（`suspended` / `suspending` / `screensaver` のいずれかと推測）。

7. **機種の食い違い**
   - この個体は PW2（Pinot/Wario, 758×1024, 249MB）。ブリーフの「PW4」と一致しない。PW4 実機で同じ調査をやり直すか、`ssh kindle` の接続先を確認する必要がある。
   - `docs/spike-kual.md` の 1072×1448 という記述は、この個体には適用できない。

8. **ブックマーク DB の書き込み**
   - 読み取りのみ実施。書き込みはブラウザ停止中に行う必要がある（WebKit のメモリキャッシュに上書きされる）。`sqlite3` の更新が FAT 上で安全かも未検証。

9. **`initctl` のフル結果**
   - 97 行（うち 53 が `start/running`）の全出力は取得済みだが、本稿では lab126_gui 関連と主要ジョブのみ抜粋した。必要なら `ssh kindle /sbin/initctl list` を再実行。

---

## 付録: 設計に効く事実のまとめ（推測を含む）

- **ブラウザ起動**: `lipc-set-prop com.lab126.appmgrd start 'app://com.lab126.browser?view=<encodeURIComponent(URL)>'` が最有力。ブラウザは WAF（mesquite）で、`state.view` を `navigator.go` に渡す。`url=` は効かない見込み。
- **URL の制約**: `http:` / `https:` / `file:///mnt/us/...` のみ。`file://` は `/mnt/us` 配下に限定。
- **スリープ検知**: `lipc-wait-event -m -t -s <sec> com.lab126.powerd 'goingToScreenSaver,outOfScreenSaver,wakeupFromSuspend,resuming,readyToSuspend,userShutdown'`。タイムアウトは rc=255。
- **スリープ抑止**: `preventScreenSaver`（rw Int）。既存コードは 1 を書いて抑止している。
- **電源ボタン**: `/dev/input/event0`（max77696-onkey）の KEY_POWER のみ。タッチは `/dev/input/event1`（cyttsp4_mt, MT protocol B）。
- **描画**: FBInk は 758×1024 / 8bpp。`fbink -e` で毎回情報取得可能。`eips` も併用可。
- **既存の拡張形態**: `/mnt/us/extensions/<name>/` + `config.xml` + `menu.json` が KUAL 形式。KUAL 本体は `/opt/amazon/ebook/booklet/KUALBooklet.jar`、appreg に `com.mobileread.ixtab.kindlelauncher` として登録済み。
- **時刻**: 端末は UTC。tzdata が無いので `TZ=JST-9` を使うのが安全。`settz` は固定オフセットの tz を生成する。
