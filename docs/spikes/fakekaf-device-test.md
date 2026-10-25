# fakekaf 実機テスト: cvm 無し + 電源ボタン / 全画面ブラウザ（FW 5.12.2.1.1）

2026-10-23。Paperwhite 2（758×1024）/ FW 5.12.2.1.1 / `ssh kindle`（<kindle-ip>, root）。
リモートのコマンドはすべて先頭に `export PATH=/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin DISPLAY=:0;` を付けて実行した。
時刻は端末の UTC。画面は `scripts/dev/kshot.sh`、タップは `scripts/dev/ktap.sh`（画像は `scripts/dev/shots/`、git 管理外）。

制約として守ったこと: `mntroot rw` / rootfs への書き込み / upstart ジョブの追加・編集 / reboot / `fw_setenv` / `dd` / `preventScreenSaver` / `com.lab126.browser go` は未使用。書き込みは `/mnt/us/DONT_START_FRAMEWORK` と `/tmp` のログだけ。`stop`/`start lab126_gui` は **2 往復**（手順 1 と 7）。スクリーンセーバー状態は最長でも約 3.9 秒で抜けた。

---

## 結論（要約）

1. **cvm 無し + fakekaf 常駐で電源ボタンは 3 回とも効いた。** `lipc-set-prop -i com.lab126.powerd powerButton 1` で `goingToScreenSaver` が来て `state=screenSaver` になり、3.7 秒以内にもう一度押すと `outOfScreenSaver` で `active` に戻った（3/3 OK）。
2. **スクリーンセーバー中は白でも直前の画面でもなく、Kindle 標準のスクリーンセーバー画像が出た。** 2 回採取した画像は別々（レタープレス文字 / 万年筆とインク壺）で、blanket の `screensaver` モジュール（X ウィンドウ `0x400002`）が描いていた（ログとウィンドウからの推定）。Java の `com.lab126.screensaver` が無くても画像は出る。
3. **3.7〜3.9 秒の範囲では suspend に進まず、SSH も切れなかった。** `/var/log/messages` に suspend/wake の行は無い。ただし深いサスペンドに入る手前で毎回戻しているので、**実際の suspend からの復帰は未確認**。
4. **復帰後の Wi-Fi は生きている。** `wpa_state=COMPLETED`、ゲートウェイ `<gateway-ip>` へ ping 成功、`wifid cmState=CONNECTED`。
5. **同じ構成で純正ブラウザは 758×1024+0+0 の全画面で表示できた**（example.com）。ブラウザ表示中に電源ボタンを往復しても、戻った後は同じウィンドウ・同じページがそのまま表示された。
6. **ブラウザの `kill 1` は従来どおり「アプリケーションエラー」ダイアログを出す。** (597,602) のタップで閉じると全面白に戻る。
7. **fakekaf のコードは変更せず、そのまま動いた。** 必要な処理（`frameworkStarted=1` の登録、`splash` の unload、`FrameworkStarted` 送信）は既存実装で足りている。
8. **復帰は正常。** fakekaf を SIGTERM → `stop lab126_gui` → フラグ削除 → `start lab126_gui` で約 111 秒後に純正ホーム。`/mnt/us/DONT_START_FRAMEWORK` 無し、fakekaf 停止、cvm 起動、`frameworkStarted=1`、blanket の `load` に `splash` 無しを確認。

---

## 0. 実行前の状態

```text
lab126_gui     start/running
framework      start/running, process 13156（cvm pid 13503）
pillow / kb / x  start/running
powerd state   active
blanket load   screensaver langpicker blankwindow usb（splash は無し）
/mnt/us/DONT_START_FRAMEWORK  無し
/mnt/us/khome-spike/fakekaf   1163136 バイト、2026-10-23 06:27
lipc-get-prop com.lab126.kaf frameworkStarted = 1（本物の kaf）
```

画面: `51-baseline.png`（純正ホーム）。

---

## 1. cvm 無しで GUI を起動する

```sh
/sbin/stop lab126_gui                 # rc=0
touch /mnt/us/DONT_START_FRAMEWORK    # 0 バイト
/sbin/start lab126_gui                # rc=0（06:30:07）
```

8 秒後（06:30:17）:

```text
lab126_gui     start/running
framework      stop/waiting          <- cvm は起動しない
framework_setup stop/waiting
pillow         start/running, process 15375
kb             start/running, process 15353
x              start/running, process 2541
splash         start/running, process 15344
pidof cvm      -> 無し
powerd state   active
blanket load   screensaver langpicker blankwindow usb splash
xwininfo       ... module:blanket-splash 758x1024+0+0  MapState=IsViewable
```

画面: `52-boot-splash-flag.png`（起動スプラッシュ。`no-cvm-browser-spike.md` 2 節と同じ）。

判定: **OK（cvm は止まり、X / blanket / pillow / appmgrd / powerd は動いている。splash は載ったまま）**。

---

## 2. fakekaf を常駐起動する

```sh
nohup /mnt/us/khome-spike/fakekaf >/tmp/fakekaf.log 2>&1 &
sleep 2
```

`/tmp/fakekaf.log`:

```text
fakekaf: unloading blanket splash module
fakekaf: registered com.lab126.kaf, frameworkStarted=1, sent FrameworkStarted
```

確認:

```text
# lipc-probe -v com.lab126.kaf
	r 	Int	frameworkStarted	[1]

# lipc-get-prop com.lab126.kaf frameworkStarted
1

# lipc-get-prop com.lab126.blanket load
screensaver langpicker blankwindow usb

# lipc-get-prop com.lab126.powerd state
active
```

- `pidof fakekaf` = 15799（常駐）。
- `xwininfo -root -tree` から `module:splash` のウィンドウが消えた（`module:screensaver` は `IsUnMapped` のまま存在）。
- 画面: `53-fakekaf-splash-unloaded.png` — **全面白**。

判定: **OK（サービス登録・`frameworkStarted=1`・splash unload・`FrameworkStarted` 送信がすべて動作）**。

---

## 3. 電源ボタン（ソフトウェア）を 3 回往復する

イベント監視:

```sh
nohup lipc-wait-event -m -t com.lab126.powerd '*' >/tmp/pb.log 2>&1 &
```

各回とも次の順で実行した（`state` は押下 0.5 秒後に読んだ値）:

```sh
lipc-set-prop -i com.lab126.powerd powerButton 1
sleep 0.5; lipc-get-prop com.lab126.powerd state
sleep 3
lipc-set-prop -i com.lab126.powerd powerButton 1
sleep 1;   lipc-get-prop com.lab126.powerd state
```

結果:

| 回 | 1 回目押下 (uptime) | 0.5 秒後の state | 2 回目押下 (uptime) | 1 秒後の state | スクリーンセーバー滞在 |
| --- | --- | --- | --- | --- | --- |
| 1 | 12910.39 | `screenSaver` | 12914.13 | `active` | 3.73 秒（06:30:39.457 → 06:30:43.187） |
| 2 | 12934.09 | `screenSaver` | 12937.71 | `active` | 3.66 秒（06:31:03.145 → 06:31:06.810） |
| 3 | 12941.64 | `screenSaver` | 12945.26 | `active` | 3.67 秒（06:31:10.699 → 06:31:14.373） |

`/tmp/pb.log`（抜粋）:

```text
[06:30:39.457381] goingToScreenSaver 2
[06:30:43.169182] t1TimerReset
[06:30:43.187906] outOfScreenSaver 1
[06:30:43.207082] exitingScreenSaver
[06:31:03.145956] goingToScreenSaver 2
[06:31:06.792044] t1TimerReset
[06:31:06.810817] outOfScreenSaver 1
[06:31:06.830470] exitingScreenSaver
[06:31:10.699470] goingToScreenSaver 2
[06:31:14.373110] outOfScreenSaver 1
[06:31:14.387815] exitingScreenSaver
```

`/var/log/messages` の powerd 行（1 回目）:

```text
063039 powerd[1894]: I def:pbprop:powerButton=1s:set powerButton
063039 powerd[1894]: I def:metrics::prg_name=powerd,prg_source=active_handle_power_button,name=GoingToScreensaverDueToPowerButtonPress,value=1
063039 powerd[1894]: I def:statech:prev=ACTIVE, next=SCREEN SAVER:State change: ACTIVE -> SCREEN SAVER
063039 powerd[1894]: I lipc:evts:name=goingToScreenSaver, origin=com.lab126.powerd, fparam=2:Event sent
063039 powerd[1894]: I def:powerd:low_temp_state:g_is_screensaver_drawn = (1)
063043 powerd[1894]: I def:pbprop:powerButton=1s:set powerButton
063043 powerd[1894]: I def:statech:prev=SCREEN SAVER, next=ACTIVE:State change: SCREEN SAVER -> ACTIVE
063043 powerd[1894]: I lipc:evts:name=outOfScreenSaver, origin=com.lab126.powerd, fparam=1:Event sent
```

`powerd` の metrics に `GoingToScreensaverDueToPowerButtonPress` が出ており、**直前の idle タイマーではなくボタン押下が契機**であることが確認できる。

**`Splash screen is on.  Ignoring power button` はテスト全体で 1 回も出ていない**:

```sh
grep -n "Ignoring power button" /var/log/messages   # 出力なし
```

`suspend` / `wake` のログも無い:

```sh
grep -inE "suspend|sleep|wake" /var/log/messages    # 出力なし
```

画面: 1 回目のスクリーンセーバー中 `54-saver-1.png`、復帰後 `55-after-saver-1.png`（全面白）。

判定: **OK（3/3 回。gate は通っており、状態遷移も出ている）**。

---

## 4. スクリーンセーバー中の画面

- `54-saver-1.png`（1 回目）: **レタープレス風の活字の画像**（Kindle 標準のスクリーンセーバー画像）。
- `57-browser-saver.png`（ブラウザ表示中の往復）: **万年筆とインク壺の画像**。1 回目とは別の画像。
- スクリーンセーバー前（`53-...`）と後（`55-...`）はどちらも全面白なので、**直前のフレームが残っているわけではない**。
- 白でも空白でもなく、画像が出る。2 回とも異なる画像だったので、組み込み画像からランダムに選ばれているとみられる（推定）。

描画元の根拠（`/var/log/messages`）:

```text
063039 blanket[3046]: I x11Handler:X11_EVENT:win=400002,xev_type=12,module=screensaver:
063043 blanket[3046]: W def:lipc:method=LipcOpenEx service=com.lab126.screensaver:Method has been deprecated.
063043 blanket[3046]: I lipc:evts:name=unmap_screensaver, origin=com.lab126.screensaver:Event sent
```

X ウィンドウは blanket の `0x400002 "L:SS_N:screenSaver_FH:F_module:screensaver_ID:blanket-screensaver_FS:F_O:U"`（実行前と復帰後は `IsUnMapped`）。`goingToScreenSaver` の時刻に blanket がこのウィンドウ（`win=400002`）の X イベントを処理し、復帰時に `unmap_screensaver` を送っている。以上から、**Java の `com.lab126.screensaver` サービスが無くても blanket の screensaver モジュールが画像を描く**と判断した（`fakekaf.md` 1.5 の「描画は blanket 側」という読みと一致。スクリーンセーバー中に `xwininfo` は取っていないため、Map 状態そのものは未観測）。

判定: **OK（純正の画像が出る）**。

---

## 5. 復帰後の Wi-Fi

```text
# wpa_cli -i wlan0 status | grep -E "^(ssid|wpa_state|ip_address)="
ssid=<SSID>
wpa_state=COMPLETED
ip_address=<kindle-ip>

# ping -c 1 -W 3 <gateway-ip>
64 bytes from <gateway-ip>: seq=0 ttl=64 time=2.193 ms
1 packets transmitted, 1 packets received, 0% packet loss

# lipc-get-prop com.lab126.wifid cmState
CONNECTED
```

判定: **OK（電源ボタン往復後も Wi-Fi は維持）**。インターネット宛の疎通は今回も試していない（ブラウザのページ表示は成功している）。

---

## 6. 全画面ブラウザ（同じ構成）

### 6.1 起動

```sh
lipc-set-prop com.lab126.appmgrd start 'app://com.lab126.browser?view=https%3A%2F%2Fexample.com%2F'   # rc=0
```

6 秒後:

```text
lipc-get-prop com.lab126.appmgrd activeApp  -> com.lab126.browser
pidof mesquite                              -> 16157
xwininfo -root -tree:
  0x1200021 "L:A_N:application_PC:TS_ID:com.lab126.browser": ("mesquite" "Mesquite")
            758x1024+0+0  +0+0  MapState=IsViewable
xwininfo -id 0x1200021:
  Absolute upper-left X: 0 / Y: 0
  Width: 758 / Height: 1024
  Map State: IsViewable
```

- `titleBar` / `searchBar` のウィンドウは無い。
- `blanket load` は `screensaver langpicker blankwindow usb`（splash 無し）。
- 画面: `56-browser-fullscreen.png` — example.com が上端から下端まで全面表示（ステータスバー・ツールバー無し）。

判定: **OK（cvm 無し + fakekaf でも 758×1024 の全画面で表示）**。`no-cvm-browser-spike.md` と同じ結果。

### 6.2 ブラウザ表示中に電源ボタンを 1 往復

```sh
lipc-set-prop -i com.lab126.powerd powerButton 1   # rc=0
# 0.5 秒後 state=screenSaver、スクリーンセーバー画像を採取（57-browser-saver.png）
lipc-set-prop -i com.lab126.powerd powerButton 1   # rc=0
# 1.5 秒後 state=active
```

`/tmp/pb.log`:

```text
[06:31:48.184303] goingToScreenSaver 2
[06:31:52.059021] t1TimerReset
[06:31:52.077404] outOfScreenSaver 1
[06:31:52.106084] exitingScreenSaver
```

スクリーンセーバー滞在は 3.89 秒。

復帰後:

```text
lipc-get-prop com.lab126.appmgrd activeApp -> com.lab126.browser
xwininfo -id 0x1200021 -> Width 758 / Height 1024 / Map State: IsViewable
```

画面: `57-browser-saver.png`（スクリーンセーバーの万年筆画像）、`58-browser-after-saver.png`（**example.com がそのまま全面表示に戻った**）。再描画の白抜けやダイアログは無かった。

判定: **OK（スクリーンセーバーから戻るとブラウザの画面も戻る）**。

### 6.3 閉じる

```sh
lipc-set-prop com.lab126.browser kill 1   # rc=0
```

3 秒後:

```text
pidof mesquite                 -> 無し
lipc-get-prop activeApp        -> 空
xwininfo                       -> "com.lab126.browser" のウィンドウ無し
0xc00021 "L:D_N:pillowAlert_ID:system_PILLOW:simple-alert_M:true_SE:simpleAlertShow"
         636x295+57+360  MapState=IsViewable
```

画面: `59-browser-kill-dialog.png` — 「アプリケーションエラー / 選択されたアプリケーションを起動できませんでした。もう一度お試しください。」（`no-cvm-browser-spike.md` 7 節と同じ）。

ダイアログの「閉じる」をタップして閉じた:

```sh
scripts/dev/ktap.sh 597 602
```

3 秒後: `pillowAlert` のウィンドウ無し、ブラウザ無し、`powerd state=active`。画面は全面白（`60-alert-dismissed.png`）。

判定: **OK（kill で閉じられる。ただし毎回ダイアログが出るので、閉じるタップまでが 1 セット）**。

---

## 7. 復帰（必須）

```sh
kill -TERM $(pidof fakekaf)          # pid 15799, 2 秒後に消滅
cat /tmp/fakekaf.log                 # 末尾に "fakekaf: exiting"
/sbin/stop lab126_gui                # rc=0（06:32:57）
rm -f /mnt/us/DONT_START_FRAMEWORK   # 直後に ls で「無し」を確認
/sbin/start lab126_gui               # rc=0（06:32:58）
```

経過:

```text
06:33:21  cvm pid 17029、framework start/running、activeApp はまだ空
06:33:42  activeApp 空
06:34:02  activeApp 空
06:34:22  titleBar のウィンドウは存在するが IsUnMapped
06:34:49  activeApp=com.lab126.booklet.home、titleBar 758x41+0+0 /
          searchBar 758x75+0+41 が IsViewable、blanket load から splash が消えた
```

所要: start 発行から純正ホームまで **約 111 秒**（06:32:58 → 06:34:49）。

最終状態（06:34:56）:

```text
/mnt/us/DONT_START_FRAMEWORK : 無し
fakekaf                      : 無し（pidof 空、リッスンも無し）
cvm                          : pid 17029
lab126_gui                   : start/running
framework                    : start/running, process 16682
framework_setup              : stop/waiting
pillow / kb / x              : start/running
splash                       : stop/waiting
powerd state                 : active
activeApp                    : com.lab126.booklet.home
com.lab126.kaf frameworkStarted : 1（本物の kaf）
blanket load                 : screensaver langpicker blankwindow usb（splash 無し）
titleBar / searchBar / home  : IsViewable
/var/local/upstart/          : 空（reboot カウンタ無し）
```

画面: `61-final-home.png`（My Kindle / マイライブラリ）。

判定: **OK（純正・フラグ無し・fakekaf 停止）**。

---

## 8. 判定まとめ

| 手順 | 内容 | 判定 | 主な根拠 / 画面 |
| --- | --- | --- | --- |
| 1 | `stop` → フラグ → `start` で cvm 無しの GUI | OK | framework stop/waiting、splash あり（`52`） |
| 2 | fakekaf 常駐（splash unload + FrameworkStarted） | OK | `frameworkStarted [1]`、load から splash 消滅（`53`） |
| 3 | 電源ボタン 3 往復 | OK 3/3 | `goingToScreenSaver` → `screenSaver` → `outOfScreenSaver` → `active` |
| 4 | スクリーンセーバー中の画面 | OK | 純正画像が 2 種類（`54`, `57`）。blanket の screensaver モジュール |
| 5 | 復帰後の Wi-Fi | OK | `wpa_state=COMPLETED`、GW ping 0% loss |
| 6 | 全画面ブラウザ | OK | `0x1200021 758x1024+0+0`（`56`） |
| 6 | ブラウザ表示中の電源ボタン往復 | OK | 3.89 秒で復帰、ブラウザのウィンドウ・ページとも維持（`58`） |
| 6 | ブラウザを閉じる | OK（ダイアログ付き） | `kill 1` → エラーダイアログ（`59`）→ タップで閉じる（`60`） |
| 7 | 復帰 | OK | 約 111 秒で純正ホーム（`61`）、フラグ無し、fakekaf 停止 |

コード変更: **なし**（`kindle/fakekaf/` は触っていない）。

---

## 9. 未確認・リスク

1. **物理の電源ボタンは押していない。** 確認したのはソフトウェアの `powerd powerButton 1` だけ。`no-framework-spike.md` 3.2 のとおり event0 の読み手が特定できておらず、物理ボタンの経路は未確認のまま。ただし powerd の gate（kaf の `frameworkStarted` と blanket の `splash`）は同じなので、物理ボタンでも通る公算が大きい（推定）。
2. **深いサスペンドは未確認。** 滞在を最長 3.89 秒に抑えたため、`t2` タイマーによる suspend に入る前後に何が起きるかは試していない。`/var/log/messages` に suspend/wake の記録は無い。5 秒超の放置は禁止条件のため実施していない。
3. **idle タイマーによる自動スクリーンセーバーは未確認。** 今回はすべてボタン押下で入れた。放置で入った場合も blanket が画像を描くかは未検証（同じ経路なので描く可能性が高い、推定）。
4. **スクリーンセーバー中に fakekaf が死んだ場合**は未確認。状態が `screenSaver` のまま残る可能性がある。
5. **`splash` は `start lab126_gui` のたびに載る。** 今回は fakekaf の unload で消えた。cvm を止める構成では毎回この処理が必要（`no-cvm-browser-spike.md` 10 節と同じ）。
6. **`splash` ジョブ自体は動き続ける。** モジュールを unload した後も `bootSplashProgress` イベント（68〜74% など）が `/var/log/messages` に出ていた。描画されないので実害は無いが、ログは賑やかになる。
7. **ブラウザを閉じると必ずエラーダイアログが出る。** 自動化するなら (597,602) のタップか、appmgrd 側の扱いの変更が要る（前回スパイクからの既知事項）。
8. **`/mnt/us` に前回のコアダンプが残っている。** `mesquite_11178_...core`（106,438,440 バイト、06:21）はこの作業より前のもので、今回のテストでは増えていない（`go` を使っていない）。削除はしていない。
