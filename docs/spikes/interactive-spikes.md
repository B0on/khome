# 実機スパイク（操作を伴うもの）

2026-10-23、Paperwhite 2 / FW 5.12.2.1.1、`ssh kindle`（<kindle-ip>）で実施。読み取り調査は [device-survey.md](device-survey.md)。

## 1. ブラウザを URL 付きで起動する — 成功

```sh
lipc-set-prop com.lab126.appmgrd start 'app://com.lab126.browser?view=https%3A%2F%2Fexample.com%2F'
```

- rc=0。8 秒後の `activeApp` は `com.lab126.browser`。
- `/var/log/messages`: `com.lab126.browser:main::navigating to provided URL` → `stored lastUrl` → `added to history`。
- appmgrd のイベント順: browser `load` → `go` → `appActivating` → `appStarted` → home(`com.lab126.booklet.home`) `pause` → `appPaused`。
- URL は `encodeURIComponent` 相当でエンコードして `view=` に入れる。

## 2. スリープ防止

- `preventScreenSaver 1` は**使わない**。自動スリープだけでなく電源ボタンも無視される（powerd: `Prevent screen saver set.  Ignoring power button`）。
- `lipc-set-prop com.lab126.powerd touchScreenSaverTimeout <任意の値>` は値に関係なく idle タイマーを 600 秒に戻す（`t1TimerReset` イベント）。残り 571s → 600s を確認。
- 採用方式: 4 分ごとに `touchScreenSaverTimeout 1` を送るループ。自動スリープは起きず、電源ボタンでは通常どおりスリープする。現状は `/tmp/survey/keepawake.sh` を手で起動しただけで、再起動で消える。ランチャーに組み込む。

## 3. 電源ボタンでのスリープと復帰

`lipc-wait-event -m -t com.lab126.powerd '*'` の記録（約 13 秒スリープ）:

```text
[03:16:43.045] goingToScreenSaver 2      # 電源ボタン。fparam=2
[03:16:56.729] t1TimerReset
[03:16:56.769] outOfScreenSaver 1        # 電源ボタンで復帰
[03:16:56.821] exitingScreenSaver
```

- powerd の状態遷移: `ACTIVE -> SCREEN SAVER`（ボタン）→ `SCREEN SAVER -> ACTIVE`（ボタン）。10 秒程度では SUSPENDED に入らない。
- スクリーンセーバー中は deviced がタッチをロック（`Lock state is set to : 1`）、復帰で 0。
- 復帰時のトリガーは `outOfScreenSaver` を使う。device-survey.md のログでは SUSPENDED を経由した場合も最後に `SCREEN SAVER -> ACTIVE` を通るので、同じイベントで拾える。
- 未確認: SUSPENDED を経由する長いスリープからの復帰で `wakeupFromSuspend` → `outOfScreenSaver` の順になるか。ランチャーは `outOfScreenSaver` だけを頼りにし、念のため `wakeupFromSuspend` も購読する。

## ランチャー設計への入力

- ホーム表示中はフレームワークを止めず、`lipc-wait-event` で `outOfScreenSaver` を待ち、届いたらホームを再描画する。
- ブラウザ・お気に入りは `appmgrd start app://com.lab126.browser?view=...` で開く。戻りは `appmgrd` の `appStateChange`/`activeApp` を監視する。
- スリープ防止は `touchScreenSaverTimeout` の定期送信。

## 4. KOReader / Rakuyomi の起動と終了（2026-10-23 12:35 JST）

SSH から `cd /mnt/us/koreader && nohup ./koreader.sh &`（既定モード）で起動。操作は SSH からのタッチ注入、確認は fb0 の吸い出しで行った（下の「開発用の道具」）。

- KOReader のファイル画面が出た。メニュー → 虫眼鏡タブ 2 ページ目に **Rakuyomi**。開くとサーバー（`plugins/rakuyomi.koplugin/server`）が起動し、Library 画面（空）が出た。ソースの追加・漫画の表示はまだ試していない。
- メニュー → Exit → Exit で終了。終了直後に koreader.sh が `appmgrd start app://com.lab126.booklet.home` を呼び、appmgrd は `booklet.home go → appActivating → appStarted`、続けて前面にいたブラウザを `pause → unload → processExited`。純正ホームが描かれ、awesome は動作中（State S）に戻った。
- **SSH の環境から起動すると PATH に `/sbin` `/usr/sbin` が無く**、koreader.sh 内の upstart の `stop` / `start`（line 297/373, 223/365）と `iptables` が `not found` で失敗した。`DISPLAY` も無く、タイトルバーの復元が `Cannot open display`。khome から起動するときは `PATH=/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin` と `DISPLAY=:0` を明示する。
- KOReader の画面右上に純正のステータスバーの時計（3:35 → 3:37）と電池表示が重なって描かれ続けた。awesome の SIGSTOP だけでは止まっていない。上の `stop` 失敗が原因かもしれない。khome が画面を持つときも同じことが起きうるので、PATH を直した状態でもう一度確かめる。

### 開発用の道具

- 画面の取得: `ssh kindle 'cat /dev/fb0'`（8bpp、line_length 768、表示 758×1024）を PGM に変換して PNG に。
- タッチの注入: `/dev/input/event1` に `struct input_event`（32bit: 16 バイト）を書き込む。座標は画面ピクセルと 1:1。押下 `ABS_MT_SLOT 0, ABS_MT_TRACKING_ID n, ABS_MT_POSITION_X/Y, BTN_TOUCH 1, SYN`、120ms 後に離す `ABS_MT_TRACKING_ID -1, BTN_TOUCH 0, SYN`。KOReader が掴んでいても効いた。
- 手元のスクリプト: `scripts/dev/kshot.sh <name>`、`scripts/dev/ktap.sh <x> <y>`。
