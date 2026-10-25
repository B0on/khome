# cvm 無し + 純正ブラウザ全画面スパイク（FW 5.12.2.1.1 実機）

2026-10-23。Paperwhite 2（758×1024）/ FW 5.12.2.1.1 / `ssh kindle`（<kindle-ip>, root）。
コマンドはすべて先頭に `export PATH=/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin DISPLAY=:0;` を付けて実行した。
時刻は端末の UTC。画面は `scripts/dev/kshot.sh`、タップは `scripts/dev/ktap.sh` で採取。
スワイプは `scripts/dev/tapgen.py` と同じ evdev 形式（`struct '<IIHHi'`）で、**リポジトリ外の一時スクリプト**が生成したバイト列を `/dev/input/event1` に書いて注入した（リポジトリのコードは変更していない）。

制約: 書き込みは `/mnt/us`・`/var/local/mesquite/browser/config.xml`・`/tmp` のみ。`mntroot rw` / rootfs / upstart ジョブ / reboot / `preventScreenSaver` は未使用。`stop`/`start lab126_gui` は **2 往復**（手順 1 と 8）だけ。

---

## 結論（要約）

1. **cvm 無しでも純正ブラウザは 758×1024+0+0 の全画面で表示できた。** 鍵は起動スプラッシュを外すことで、`lipc-set-prop com.lab126.blanket unload splash` だけで画面が白になり、その上にブラウザが全面表示される（`25-browser-launched.png`）。
2. **全画面は `config.xml` を変えなくても成立する。** cvm が無いと `com.lab126.chromebar` が存在せず、pillow の `titleBar`（41px）と `searchBar`（75px）の X ウィンドウが作られない。awesome の `topOffset` が 0 になるので、`PC:TS` のままでもアプリは 758×1024+0+0 になる（実測。理由は推定）。
3. `configureSearchBar=none` を足すとウィンドウ名は `PC:TS` → `PC:T` に変わるが、**ジオメトリは 758×1024+0+0 のまま**。cvm 有りの環境では titleBar 41px が残る方向なので、この構成では**不要**（試した上で元に戻した）。
4. pillow の `disableEnablePillow` / `chromeState=1` / `interrogatePillow(hideMe)` は、そもそも titleBar が無いので**見た目の変化なし**（`chromeState=1` は即 0 に戻る）。
5. タッチは全画面でも動く。**リンクのタップで遷移**（9.2 秒）、**スワイプでスクロール**（押下→8 点移動→離す、約 400px）を実測。
6. 外部からの操作:
   - **戻る**: `lipc-set-prop com.lab126.browser searchBarButtonSelected back` → 約 0.2 秒で前のページ（OK）。
   - **URL を開く**: `lipc-set-prop com.lab126.appmgrd start 'app://com.lab126.browser?view=<URLエンコード>'` → **同じ pid・同じウィンドウのまま**遷移（OK）。今回はブラウザが前面の状態で実行した（前面でない場合の挙動は未確認）。遷移は非同期。
   - `lipc-set-prop com.lab126.browser go <url>` は **mesquite をクラッシュさせる（NG）**。コアダンプ採取ダイアログが出て、プロセスはその後終了する。
   - `lipc-set-prop -i com.lab126.appmgrd backward 1` は**何も起きない（NG）**。
7. **閉じる**: `lipc-set-prop com.lab126.browser kill 1` が唯一確実（約 2 秒で pid 消滅）。ただし閉じた直後に appmgrd が**「アプリケーションエラー」ダイアログ**を出し、「閉じる」をタップするまで画面に残る。`appmgrd stop` と `browser unload` は効かない（前者は framework 未初期化で失敗）。
8. 復帰は正常。`stop lab126_gui` → フラグ削除 → `start lab126_gui` で **127 秒**後に純正ホーム（`46-recover-100s.png`）。config.xml は `cmp` 一致、フラグ無しを確認済み。

---

## 0. 実行前の状態

```text
lab126_gui           start/running
framework            start/running, process 4071（cvm）
pillow / kb          start/running
x / awesome / blanket / appmgrd / powerd  起動済み
/mnt/us/DONT_START_FRAMEWORK              無し
blanket の load リスト                     screensaver langpicker blankwindow usb（splash は無し）
lipc-get-prop com.lab126.winmgr chromeState = 0
```

画面: `20-baseline.png`（純正ホーム）。

`/var/local/mesquite/browser/config.xml` は 3625 バイト、`<kindle:chrome>` 無し、`viewmodes="application"`（`config.xml:6`、`:37-43` が gestures、`:77-80` が messaging）。

---

## 1. `stop lab126_gui` → `DONT_START_FRAMEWORK` → `start lab126_gui`

```sh
/sbin/stop lab126_gui          # rc=0, /proc/uptime 11983.11 -> 11983.73 = 0.62 秒
touch /mnt/us/DONT_START_FRAMEWORK
/sbin/start lab126_gui         # rc=0（コマンドは即復帰）
```

`stop` 直後（1 秒後）:

```text
残ったプロセス: powerd(1894) appmgrd(1899) Xorg(2615) awesome(3045) blanket(3046)
cvm: 無し
initctl: lab126_gui / framework / pillow / kb / splash すべて stop/waiting
```

`stop` 直後の画面は **全面白**（`21-gui-stopped.png`）。cvm が描かないため。

`start` の 7 秒後（06:15:25）:

```text
lab126_gui   start/running
framework    stop/waiting          <- cvm は起動しない（フラグ）
pillow       start/running, process 10136
kb           start/running, process 10114
splash       start/running, process 10105
blanket load: screensaver langpicker blankwindow usb splash   <- splash が載っている
powerd state: active
chromeState: 0
```

画面: `22-boot-splash.png` — 起動スプラッシュ（kindle ロゴ + プログレスバー約 10%）。
`xwininfo` ではスプラッシュは X のクライアントウィンドウだった:

```text
0x400079 "L:SS_N:dialog_RC:0_FH:F_module:splash_BW:0_ID:blanket-splash_FS:F_O:U"  758x1024+0+0  MapState=IsViewable
0x400003 "L:A_N:application_module:blankwindow_ID:blankBackground_WS:true"        758x1024+0+0  MapState=IsViewable
```

判定: **OK（cvm は止まり、pillow / kb / splash は起動、画面はスプラッシュ）**。前回スパイク（`no-framework-spike.md` 6 節）と同じ。

---

## 2. 起動スプラッシュを消す

### 2.1 framework が普段やっていること（根拠）

`/etc/upstart/framework_setup.conf` の末尾（`FrameworkStarted` を受けた後）:

```sh
    # unload the module
    f_blanket_unload_module splash

    # notify powerd that there is no splash screen anymore
    lipc-send-event -r 5 com.lab126.hal bootSplashCleanup
```

`f_blanket_unload_module()` の実体は `/etc/upstart/blanket_functions` にあり、
`lipc-set-prop com.lab126.blanket unload -s "$MODULE"` を送って `load` リストから消えるまで最大 5 秒ポーリングする。

`/etc/upstart/splash.conf` は `start on start_splash` / `stop on stopping lab126_gui` のタスクで、
`SPLASH_PROGRESS` を 2% ずつ 94% まで上げながら `splash_progress()`（= `lipc-send-event com.lab126.hal.splash bootSplashProgress -i N`）を送る。
`start_splash` は `lab126_gui.conf` の pre-start が `f_emit` する。pre-start では先に `splash_init`（splash モジュールの load + `bootSplashInit`）も走る。

**正常起動時の実ログ**（今回の復帰時、`/var/log/messages`）:

```text
261023:062449 lipc-send-event: name=bootSplashProgress, origin=com.lab126.hal.splash, fparam=94
261023:062520 system: I blanket_f:unloading blanket module:splash:splash
261023:062520 blanket[3046]: I loaderEventHandler:MODULE_UNLOAD_EVENT:module=splash
261023:062520 lxinit[2541]: I def:main:got unloaded module:splash
261023:062521 powerd[1894]: I def:ipc_event_cb:Handling usb hal event = bootSplashCleanup
261023:062521 lipc-send-event: name=bootSplashCleanup, origin=com.lab126.hal
```

つまりスプラッシュは **X ウィンドウ（blanket の splash モジュール）** で、消す操作は「モジュール unload」＋「powerd への `bootSplashCleanup` 通知」の 2 つ。

### 2.2 試したコマンドと結果

**試したこと（成功）**: モジュールの unload だけ。

```sh
lipc-set-prop com.lab126.blanket unload splash     # rc=0, real 0.03s
```

- 直前の `lipc-probe -v com.lab126.blanket` の `load` = `screensaver langpicker blankwindow usb splash`
- 直後の `load` = `screensaver langpicker blankwindow usb`
- `xwininfo` から `0x400079 ... module:splash ...` が**即座に消えた**（他のウィンドウは不変）。
- 画面は**全面白**になった（`23-splash-unloaded.png`）。e-ink の残像も無く、スプラッシュの画素は消えた。

12 秒後も白のまま（`24-white-12s.png`）。この時点で `splash` ジョブは `stop/waiting`（94% まで進んで終了）。モジュールが無いので `bootSplashProgress` イベントは描画されない。

**送らなかったもの**: `lipc-send-event com.lab126.hal bootSplashCleanup`。
powerd の「スプラッシュ中」判定を解除するとスリープ系の挙動が変わり得るが、フラグを置いたままのスリープ試験は禁止なので、**今回は送っていない**（下記「未確認」）。

判定: **OK（`unload splash` だけでスプラッシュは消え、画面が使える）**。

---

## 3. ブラウザ起動

```sh
lipc-set-prop com.lab126.appmgrd start 'app://com.lab126.browser?view=https%3A%2F%2Fexample.com%2F'
# rc=0
```

- `activeApp=com.lab126.browser` まで **約 1.5 秒**（0.5 秒間隔のポーリングで 12045.80 は home、12047.16 で browser）。
- mesquite のログ: `Statistics::Load took: 0.6s`、`contentWindow::document loaded` が 06:16:18（発行は 06:16:14）。
- 画面: `25-browser-launched.png` — example.com が**上端から下端まで**表示され、ステータスバーもツールバーも無い。

`xwininfo -root -tree`:

```text
0x1200021 "L:A_N:application_PC:TS_ID:com.lab126.browser": ("mesquite" "Mesquite")  758x1024+0+0  +0+0  MapState=IsViewable
0x400003  "L:A_N:application_module:blankwindow_ID:blankBackground_WS:true"          758x1024+0+0
0xe00003  "L:KB_N:keyboard_...": ("kb" "Kb")                                         758x352+0+672
```

- `titleBar` / `searchBar` のウィンドウは**存在しない**。
- `xwininfo -id 0x1200021`: Absolute upper-left (0,0)、Width 758、Height 1024。
- `kb`（キーボード）のウィンドウは下端に mapped のまま残っているが、ブラウザが全面を覆うので見えない。

理由（ログ）: `com.lab126.chromebar` が **ServiceUnknown**。

```text
261023:061618 mesquite[10879]: E lipc:dbuserr:... The name com.lab126.chromebar was not provided by any .service files
261023:061618 mesquite[10879]: E com.lab126.browser:...configureChrome... --error: lipcErrNoSuchSource
```

ブラウザは `pillow.configure` / `configureChrome` / `setTitleBar` を送るが、受け手が無い。その結果 pillow の chrome ウィンドウが作られず、awesome の `topOffset` が 0 になる＝**全画面**（推定。観測事実は「chrome ウィンドウ 0 個 + 758x1024+0+0」）。

判定: **OK（cvm 無しでもブラウザは起動し、全画面でページが出る）**。

---

## 4. 全画面化の検証

### 4.1 バックアップ

```sh
cd /var/local/mesquite/browser
cp -p config.xml config.xml.khome-orig
cmp config.xml config.xml.khome-orig   # rc=0
md5sum: 8b56cfc40698bae54be8d25ca9ef6ad9（両方）
```

### 4.2 `configureSearchBar=none` を追加

`</kindle:gestures>` の直後に次を挿入して起動し直した（`/tmp/config.new` 経由で `cat > config.xml`）:

```xml
    <!-- Disable system chrome -->
    <kindle:chrome>
        <kindle:asset key="configureSearchBar" value="none" />
    </kindle:chrome>
```

ブラウザを `kill` → `appmgrd start` で再起動した結果:

| 項目 | 変更なし | `configureSearchBar=none` |
| --- | --- | --- |
| ウィンドウ名 | `PC:TS` | **`PC:T`** |
| ジオメトリ | 758x1024+0+0 | **758x1024+0+0（同じ）** |
| ページ表示 | OK | OK（`28-config-chrome-none.png`） |
| titleBar / searchBar | 無し | 無し |

判定: **効果は「PC の S が消える」だけ。この構成（cvm 無し）では全画面化に不要。**
cvm 有りなら `PC:T` は titleBar 41px を残す指定なので、**逆に不利**になり得る（awesome の `chrome_get_persistent_top_offset` からの推定）。よって元に戻した（4.4）。

### 4.3 pillow の titleBar を消す系

| 操作 | 結果 |
| --- | --- |
| `lipc-set-prop com.lab126.pillow disableEnablePillow disable` | rc=0。ウィンドウ構成・画面とも**変化なし**（`29-pillow-disabled.png`）。`enable` で戻した |
| `lipc-set-prop -i com.lab126.winmgr chromeState 1` | rc=0 だが直後の `lipc-get-prop` は **0**。`titleBar`/`searchBar` は 0 個。awesome 側は「no chrome to raise」で状態を戻す実装（`lab126_chrome_layer.lua` の `chrome_raise()`） |
| `lipc-set-prop com.lab126.pillow interrogatePillow '{"pillowId": "default_status_bar", "function": "nativeBridge.hideMe();"}'` | rc=0、画面・ウィンドウとも変化なし（`43-after-interrogate.png`） |
| `wmctrl` で titleBar を 1px に | **未実施**（titleBar が存在しないため対象なし） |

判定: **この構成では titleBar 自体が無いので、これらは不要（N/A）**。cvm を動かす構成に戻す場合は別途検証が要る。

### 4.4 元に戻したことの確認

```sh
cd /var/local/mesquite/browser
cp -p config.xml.khome-orig config.xml
cmp config.xml config.xml.khome-orig   # rc=0
```

元の config で再起動 → `0x1200021 "L:A_N:application_PC:TS_ID:com.lab126.browser"  758x1024+0+0`、`41-orig-config-fullscreen.png` で example.com が全面表示。**config 変更なしで全画面が再現**した。

---

## 5. タッチ（タップ・スワイプ）

### 5.1 リンクのタップ

Wikipedia（`https://en.m.wikipedia.org/wiki/Kindle`）を `appmgrd start` で開き（`30-wikipedia.png`）、「Amazon Kindle」のリンクをタップ:

```sh
scripts/dev/ktap.sh 277 588
```

- 直後のスクリーンショット（6 秒後）はまだ旧ページで、リンクにフォーカス枠が出ただけ（`31-link-tap.png`）。
- ログ: `navigator::added to history` 06:18:41 → `contentWindow::document loaded` **06:18:50**、`Load took: 9.2s`（1718.5kB）。
- さらに待つと「Amazon Kindle」記事が全面表示（`32-wait-load.png`）。

判定: **OK（タップでリンク遷移する。ただしこのページは読み込みに 9 秒かかった）**。

### 5.2 スワイプ（スクロール）

`ktap.sh` と同じ evdev 形式で、押下 → 8 点の移動 → 離上 を 50ms 間隔で `/dev/input/event1` に書いた。

```sh
# 生成したバイト列（リポジトリ外）: press.bin, move01..08.bin, release.bin
cat /tmp/survey/press.bin > /dev/input/event1
for i in 01 02 03 04 05 06 07 08; do
    cat /tmp/survey/move$i.bin > /dev/input/event1
    usleep 50000
done
cat /tmp/survey/release.bin > /dev/input/event1
```

- 座標: (380,850) → (380,450)、8 ステップ（50px ずつ）。
- 結果: ページが約 400px 上にスクロールし、右端のスクロールバーのつまみも移動（スワイプ前は `32-wait-load.png`、後は `34-after-swipe.png`）。

判定: **OK（全画面でもスワイプでスクロールできる）**。

---

## 6. 戻る／URL を開く

ブラウザ起動中だけ `com.lab126.browser` が LIPC サービスとして現れる。`lipc-probe -v com.lab126.browser` の主なプロパティ:

```text
w Str processRestart / pause / load / kill / unload / run / go
w Str searchSelection / searchBarButtonSelected / switchViewMode
w Str keyboardGetSurround / keyboardCommit / keyboardDelete / keyboardReplace / keyboardSetPreeditString
w Str wifiPopupDone / wirelessOn / captivePortalResponse / demoWifiNotAvailableAlertResponse
r Str ready
```

| 手段 | コマンド | 結果 |
| --- | --- | --- |
| ブラウザの「戻る」ボタン相当 | `lipc-set-prop com.lab126.browser searchBarButtonSelected back` | **OK**。ログ `back button selected event received` → `navigator::back` → 0.2 秒で前ページ（`36-back-prop.png`） |
| 別 URL を appmgrd で渡す | `lipc-set-prop com.lab126.appmgrd start 'app://com.lab126.browser?view=<URL>'` | **OK**。pid 11178・ウィンドウ 0x1200021 のまま Wikipedia に遷移（`30-wikipedia.png`）。アプリは再起動されない |
| appmgrd の戻る | `lipc-set-prop -i com.lab126.appmgrd backward 1` | **NG**。rc=0 だが画面・ログとも変化なし（`35-backward.png`） |
| ブラウザの `go` | `lipc-set-prop com.lab126.browser go 'https://example.com/'` | **NG（クラッシュ）**。`JunoLipcHandler.onPropertySet:Property Name-> go` の直後に SIGSEGV。`Collecting Debug Info` ダイアログ（`37-go-prop.png`）、ダンプ採取後プロセス終了 |

`go` のクラッシュは `strncmp`（libc）→ `libmesquitelipc.so.1` のスタックで、引数の形式が想定と違う可能性が高い（`{"url": ...}` 等）。**この FW では外部からの URL 指定に `go` を使わないこと**。

ダイアログを閉じた後の画面は、ページの中央が白く抜けたまま（`38-after-dump-dismiss.png`）。e-ink の再描画が追いついていない（後述）。

---

## 7. ブラウザを閉じる

| 手段 | コマンド | 結果 |
| --- | --- | --- |
| appmgrd の stop | `lipc-set-prop com.lab126.appmgrd stop com.lab126.browser` | **NG**。rc=0、2 秒後も pid 12613 は生存、`activeApp` も browser のまま。ログに `appmgr_lipc:failed_retrieving_framework_status` / `appmgr_history:framework_not_initialized`（cvm が無いので framework 状態が取れない） |
| ブラウザの unload | `lipc-set-prop com.lab126.browser unload 1` | **NG**。rc=0、`JunoApplicationManager::setValue:propertyName:unload` のログだけで終了しない |
| ブラウザの kill | `lipc-set-prop com.lab126.browser kill 1` | **OK**。約 2 秒で pid 消滅、`activeApp` 空、ウィンドウ消滅 |
| SIGTERM | `kill -TERM <pid>` | 今回は既に終了済みで `can't kill pid`。クラッシュ後に固まったプロセスには kill プロパティも効かなかった（LIPC ループが死んでいるため） |

`kill` の直後、**appmgrd が pillow のアラートを出す**:

```text
0xc00021 "L:D_N:pillowAlert_ID:system_PILLOW:simple-alert_M:true_SE:simpleAlertShow"  636x295+57+360  MapState=IsViewable
```

画面は「アプリケーションエラー / 選択されたアプリケーションを起動できませんでした。もう一度お試しください。」（`26-browser-killed.png`、`40-after-crash-exit.png`）。

- 「閉じる」（597,602）をタップすると消え、**全面白**に戻る（`27-alert-dismissed.png`）。
- このダイアログはブラウザが正常に kill された場合でも出る。**ブラウザを閉じる手段には「ダイアログを閉じる」まで含める必要がある**。
- ダイアログを閉じた直後は e-ink の残像が残ることがある（`38-after-dump-dismiss.png`）。

---

## 8. 復帰

```sh
/sbin/stop lab126_gui                 # rc=0
rm -f /mnt/us/DONT_START_FRAMEWORK    # 無いことを確認
cmp /var/local/mesquite/browser/config.xml /var/local/mesquite/browser/config.xml.khome-orig  # rc=0
/sbin/start lab126_gui                # rc=0（06:24:02）
```

経過:

```text
06:24:02  start
06:24:36  framework start/running（framework_setup も start/running）
06:25:19  cvm が FrameworkStarted を送信（発行から 77 秒）
06:25:20  blanket_f が splash モジュールを unload
06:25:21  bootSplashCleanup を powerd が受信
06:25:36  kshot: まだスプラッシュの残像（プログレスバー約 48%）
06:26:09  純正ホーム表示（発行から約 127 秒）
```

- `45-recover-75s.png` の「48%」は、06:24:49 に 94% まで進んでいた後の**部分更新の残像**とみられる（推定）。新しいスプラッシュではない（ログに 94% の後に unload がある）。ホームが出るまで消えなかった。
- 最終画面: `46-recover-100s.png`（My Kindle / マイライブラリ、ステータスバーとツールバーあり）。

最終状態:

```text
/mnt/us/DONT_START_FRAMEWORK : 無し
config.xml                   : config.xml.khome-orig と cmp 一致（md5 8b56cfc4...）
lab126_gui                   : start/running
framework                    : start/running（cvm pid 13503、/var/run/cvm.pid と一致）
pillow / kb / x              : start/running
awesome / blanket / appmgrd / powerd : 起動
activeApp                    : com.lab126.booklet.home（pid 13503）
titleBar 758x41+0+0 / searchBar 758x75+0+41 / home 758x908+0+116 : すべて IsViewable
powerd state                 : active
```

判定: **OK（純正・フラグ無し・config 元通り）**。

---

## 9. 全画面ブラウザの再現手順

```sh
export PATH=/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin DISPLAY=:0

# 1. cvm を止めて GUI を起動し直す
/sbin/stop lab126_gui
touch /mnt/us/DONT_START_FRAMEWORK
/sbin/start lab126_gui

# 2. 7 秒ほど待ってスプラッシュを外す（画面が白くなる）
lipc-set-prop com.lab126.blanket unload splash

# 3. ブラウザを全画面で開く（URL は URL エンコードする）
lipc-set-prop com.lab126.appmgrd start 'app://com.lab126.browser?view=https%3A%2F%2Fexample.com%2F'

# 4. 戻る
lipc-set-prop com.lab126.browser searchBarButtonSelected back

# 5. 別の URL へ（同じウィンドウ・同じプロセスのまま）
lipc-set-prop com.lab126.appmgrd start 'app://com.lab126.browser?view=<URLエンコード>'

# 6. 閉じる（直後に出る「アプリケーションエラー」の「閉じる」をタップ）
lipc-set-prop com.lab126.browser kill 1

# 7. 復帰
/sbin/stop lab126_gui
rm -f /mnt/us/DONT_START_FRAMEWORK
/sbin/start lab126_gui          # ホームまで約 2 分
```

確認ポイント:

- `xwininfo -root -tree | grep com.lab126.browser` が `758x1024+0+0` で `MapState=IsViewable`。
- `titleBar` / `searchBar` が 1 つも無いこと。
- `lipc-get-prop com.lab126.blanket load` に `splash` が無いこと。

---

## 10. 残った問題・リスク

1. **全画面は「cvm が無い副作用」で成立している。** chrome ウィンドウが無いから `topOffset=0` になる（推定）。cvm（または chromebar を提供する何か）を部分的に起動すると titleBar 41px が戻り、全画面が崩れる可能性が高い。`configureSearchBar=none` は searchBar を消すが titleBar は残す（`PC:T`）。
2. **ブラウザを閉じると必ず「アプリケーションエラー」ダイアログが出る。** appmgrd が異常終了扱いにするため。自動化するなら (597,602) のタップか、ダイアログを出さない方法（appmgrd 側の扱い）の調査が要る。
3. **`com.lab126.browser go` はクラッシュする。** URL 指定は `appmgrd start` を使うこと。
4. **クラッシュすると 106MB のコアダンプが `/mnt/us` に残る。**
   `/mnt/us/mesquite_11178_001-juno_12020101_pinot_wario-372058.core`（106,438,440 バイト）。
   さらに**純正ホームのライブラリに「本」として表示される**（`46-recover-100s.png` の 1 冊目）。今回は削除していない。運用では無効化・削除の方針が必要。
5. **e-ink の再描画が不完全。** ダイアログやオーバーレイを閉じた後、下のページが白く抜ける（`38-after-dump-dismiss.png`）。全画面 UI ではダイアログ使用後にフルリフレッシュ（`fbink -s -W GC16` 等）が必要。
6. **ページのレイアウトが 758px 幅に最適化されない。** Wikipedia はデスクトップ幅で描画され、右端が切れる（`32-wait-load.png`）。`config.xml` の `zoomFactor` / viewport 設定の調整が別途必要。
7. **`appmgrd stop` が効かない。** cvm が無いため `framework_not_initialized` になる。プロセス管理はブラウザの `kill` プロパティ（または SIGKILL）に頼るしかない。
8. **スプラッシュは `start lab126_gui` のたびに出る。** `splash_init` + `start_splash` が pre-start で走るため、cvm を止める構成では**毎回 `unload splash` が必要**。
9. **`bootSplashCleanup` を送っていない。** powerd は「スプラッシュ中」のままかもしれず、電源ボタンが無視される可能性がある（前回スパイクの `pbisplash` と同じ状態）。フラグ中のスリープ試験は禁止のため未検証。

---

## 11. 未確認

- `lipc-send-event com.lab126.hal bootSplashCleanup` を送った場合の powerd の挙動（電源ボタン・自動スリープ・画面消灯）。今回は意図的に送っていない。
- フラグを置いたまま idle タイマーが 0 になったときの挙動（今回は触操作でリセットし続けた。`Remaining time` は 484〜541 秒の範囲で観測）。
- `unload splash` の後に再度スプラッシュが必要になった場合に `lipc-set-prop com.lab126.blanket load splash` で戻せるか。
- `com.lab126.browser go` の正しいペイロード（クラッシュを避けるため追試していない）。
- `switchViewMode` / `load` / `run` など他のブラウザプロパティの意味と安全性。
- ブラウザの `unloadPolicy=unloadOnPause` により、他アプリが前面に出たときの挙動（今回は他アプリを起動していない）。
- Wi-Fi 切断・captive portal・キーボード入力（`kb` は mapped だが全画面ブラウザに覆われて見えない。URL 入力欄は無い）。

---

## 12. kshot 一覧（`scripts/dev/shots/`、git 管理外）

| ファイル | 内容 |
| --- | --- |
| `20-baseline.png` | 実行前の純正ホーム |
| `21-gui-stopped.png` | `stop lab126_gui` 直後（全面白） |
| `22-boot-splash.png` | フラグ付き `start`（スプラッシュ、約 10%） |
| `23-splash-unloaded.png` | `unload splash` 直後（全面白） |
| `24-white-12s.png` | 12 秒後も白のまま |
| `25-browser-launched.png` | 初回ブラウザ起動（example.com、全画面、config 元のまま） |
| `26-browser-killed.png` | `kill` 後のアプリケーションエラーダイアログ |
| `27-alert-dismissed.png` | ダイアログを閉じた後（全面白） |
| `28-config-chrome-none.png` | `configureSearchBar=none` で再起動（全画面のまま） |
| `29-pillow-disabled.png` | `disableEnablePillow disable` 中（変化なし） |
| `30-wikipedia.png` | `appmgrd start` で別 URL（同じウィンドウ） |
| `31-link-tap.png` | リンクタップ直後（まだ旧ページ、フォーカス枠） |
| `32-wait-load.png` | 遷移完了（Amazon Kindle 記事） |
| `33-before-swipe.png` | スワイプ前（`kshot.sh 33-before-swipe` は実行に失敗し未生成。代わりに `32-wait-load.png` を参照） |
| `34-after-swipe.png` | スワイプ後（約 400px スクロール） |
| `35-backward.png` | `appmgrd backward 1`（無変化） |
| `36-back-prop.png` | `searchBarButtonSelected back`（前ページに戻った） |
| `37-go-prop.png` | `go` でクラッシュ（Collecting Debug Info） |
| `38-after-dump-dismiss.png` | ダンプダイアログを閉じた後（ページ中央が白抜け） |
| `39-after-kill2.png` | クラッシュプロセス終了直後（残像） |
| `40-after-crash-exit.png` | アプリケーションエラーダイアログ |
| `41-orig-config-fullscreen.png` | config を元に戻して再起動（全画面） |
| `42-closed-dialog.png` | `kill` 後のダイアログ |
| `43-after-interrogate.png` | `interrogatePillow hideMe` 後（全面白） |
| `44-recover-30s.png` | 復帰 30 秒（スプラッシュ） |
| `45-recover-75s.png` | 復帰 75 秒（スプラッシュの残像） |
| `46-recover-100s.png` | 復帰完了（純正ホーム） |
