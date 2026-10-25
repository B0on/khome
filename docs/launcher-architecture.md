# ランチャー設計（khome）

2026-10-23。端末は Paperwhite 2（758×1024, 212dpi, armv7 soft-float, RAM 249MB, FW 5.12.2.1.1）。
根拠: [spikes/device-survey.md](spikes/device-survey.md)、[spikes/interactive-spikes.md](spikes/interactive-spikes.md)、デザイン: [../design/system/SPEC.md](../design/system/SPEC.md)。

## 方針

- 純正フレームワーク（Xorg / awesome / cvm / appmgrd / powerd）は**止めない・殺さない**。ホームを出している間だけ画面と入力を借りる。KOReader が framework 稼働中に使っているのと同じ手口（awesome を SIGSTOP、pillow を無効化、タッチを EVIOCGRAB）。
- 「純正のホームに戻ろうとした瞬間」を乗っ取る。ブラウザのホームボタンや KOReader 終了後に appmgrd が `com.lab126.booklet.home` を前面に出したら、khome が画面を取り返して描き直す。
- 常駐プロセスは 1 つ（`khome`）。C で書き、静的リンク・soft-float（zig `cc -target arm-linux-musleabi -mcpu=mpcorenovfp -static`）。
- 描画は自前（8bit グレーのキャンバス + stb_truetype + stb_image）。同じコードを Mac でビルドすると PNG を書き出せるので、デザインのモックと画面単位で突き合わせられる。画面への反映は `/dev/fb0` に書いてから FBInk CLI で refresh（後で MXCFB ioctl を直接呼ぶ形に置き換えてよい）。
- レイアウトは SPEC.md の 1072×1448 座標をそのまま「設計単位」として書き、描画時に ×(758/1072) で縮める（縦 1024/1448 とほぼ同率）。SPEC.md を唯一の座標の出典にする。

## 状態

```
          boot
            │  (upstart: start on started lab126_gui → booklet.home が出るのを待つ)
            ▼
   ┌──── HOME ◄──────────────┐◄──── outOfScreenSaver（OWN 中なら再描画）
   │  │   ▲ ▲                │
   │  │   │ └── SETTINGS ◄───┤ (設定タップ / ← ホーム)
   │  │   └──── NEWS ◄───────┤ (新聞タップ / 左上タップ)
   │  ▼                      │
   │ RELEASED(browser) ──────┤ booklet.home が前面に戻った（appmgrd イベント）
   │ RELEASED(koreader) ─────┘ koreader.sh の終了
   └► STOCK（設定「Kindle 標準画面に戻る」）: 画面を返して khome は終了。次回起動 or KUAL で再開
```

- スリープ（`asleep`）は HOME / BROWSER / SHEET / KOREADER と直交するフラグで、状態機械の入力と出力が持つ。`goingToScreenSaver` で立ち、`outOfScreenSaver` で下りる。
- スリープ中は時計・状態変化・タップ・シート引きを捨てて画面へ描かない。アプリの終了だけは状態へ反映し、復帰後の全面再描画で追いつく。
- OWN（HOME / SETTINGS / NEWS）: cvm/awesome STOP、pillow disable、タッチを EVIOCGRAB（電源キーは grab せず読むだけ）。
- RELEASED: すべて元に戻した上で、純正アプリまたは KOReader を起動し、戻りを待つ。
- 電源キー: OWN 中は HOME → HOME(await-sleep) → ASLEEP → HOME と遷移し、スリープ中だけ cvm/awesome を動かす（詳細は末尾の実測メモ）。
- 遷移は全部 `own()` / `release()` の 2 関数を通す。どちらも何度呼んでも同じ状態に収束させる（冪等）。

## 入力イベント（1 本の poll ループ）

| 源 | 取り方 | 用途 |
| --- | --- | --- |
| タッチ | `/dev/input/event1`（`/proc/bus/input/devices` から検出、決め打ちしない）、MT protocol B | タップ判定。SPEC.md のタップ矩形 |
| 電源キー | `/dev/input/event0`（`Name="max77696-onkey"` から検出、決め打ちしない）、EVIOCGRAB しない | `KEY_POWER` で cvm/awesome を一時 CONT（powerd の問い合わせ用） |
| powerd | 子プロセス `lipc-wait-event -m com.lab126.powerd goingToScreenSaver,outOfScreenSaver,wakeupFromSuspend` の stdout | 復帰で再描画 |
| appmgrd | 子プロセス `lipc-wait-event -m com.lab126.appmgrd '*'` | booklet.home の `go` / `appActivating` で取り返し |
| 子プロセス終了 | SIGCHLD（KOReader） | KOReader から戻る |
| タイマー | poll のタイムアウト | 毎分の時計更新（SPEC の時計矩形だけ部分 refresh）、240 秒ごとの `touchScreenSaverTimeout 1` |

lipc-wait-event の子が死んだら作り直す。

## アプリ

- **新聞**: khome 内の画面（NEWS）。`/mnt/us/newspaper/text/current` の text-v1（edition.tsv、index.tsv、articles/*.txt）を `kindletimes.c` が 758×1024 に組んで描画し、左右タップでページ送り、記事タップで本文、左上でホーム。取得は `kindle/extensions/newspaper/bin/fetch.sh`（<server-host> `http://<server-ip>:8088` の latest-text.json と bundle）をシェルのまま khome から子プロセスで呼ぶ。
- **漫画**: release() → `/mnt/us/koreader/koreader.sh` を子プロセスで起動（既定モード）。koreader.sh は自分で pillow / awesome を止め、終了時に元へ戻して `appmgrd start app://com.lab126.booklet.home` を呼ぶ（[spikes/koreader-install.md](spikes/koreader-install.md)）。khome は子の終了（SIGCHLD）で own() → HOME。直後に来る booklet.home の通知は OWN 中なので無視してよい。Rakuyomi を起動直後に開くには `koreader/patches/2-*.lua` のユーザーパッチを使う（`start_with` では選べない）。このパッチは AGPL 派生物の可能性があるため**別配布**（リポジトリには含めない）。
- **ブラウザ**: release() → `lipc-set-prop com.lab126.appmgrd start app://com.lab126.browser`。お気に入りタイルは `...?view=<encodeURIComponent(url)>`。
- **お気に入り**: `/mnt/us/.active_content_sandbox/browser/resource/LocalStorage/file__0.localstorage` の `ItemTable.bookmarks`（UTF-16LE の JSON）。`sqlite3 ... "select hex(value) ..."` を子プロセスで読み、C 側で UTF-16→UTF-8 と JSON（title / url）を解く。HOME に入るたびに読み直す。
- **設定**: Wi-Fi オン/オフ、フロントライト、新聞を今すぐ更新、再起動（確認つき）、Kindle 標準画面に戻る。lipc のプロパティ名（Wi-Fi、フロントライトの範囲）は実装前に実機で確かめる。

## 安全装置（実機で文鎮化させない）

- 起動時に `/mnt/us/khome/disable` があれば何もしない（USB で置ける）。
- 起動回数カウンタ: 起動時に `/mnt/us/khome/boot_attempts` を +1、HOME を 60 秒保てたら 0 に戻す。3 回連続で失敗したら自動で disable を作る。
- khome が落ちたら upstart の respawn に任せず、ラッパーが release 相当（`killall -CONT awesome`、pillow enable）を必ず実行する。
- SSH（Wi-Fi）はいつでも生きている。`ssh kindle 'touch /mnt/us/khome/disable; killall khome'` で戻せる。
- `/etc/upstart/` への登録（mntroot rw が要る）は最後。それまでは KUAL か SSH から手で起動して開発する。登録はユーザーの了承を得てから行う。

## ソース構成（予定）

```
launcher/
  src/main.c        poll ループ、状態遷移、own/release
  src/canvas.c      8bit キャンバス、矩形・線・円環（AA）、PNG ブリット
  src/text.c        stb_truetype のフォント読み込み・グリフキャッシュ・行描画
  src/layout.c      SPEC.md の座標で各画面を組む（home / settings / news）
  src/fb.c          /dev/fb0 への書き出しと refresh（Kindle ビルドのみ）
  src/png_out.c     Mac ビルドで PNG を書き出す（テスト用）
  src/input.c       タッチ・キー（evdev）
  src/lipc.c        lipc-wait-event 子プロセスと lipc-set-prop/get-prop 呼び出し
  src/favorites.c   bookmarks の読み出し・UTF-16/JSON 解析
  third_party/      stb_truetype.h, stb_image.h, stb_image_write.h
  assets/           フォント（design/system/fonts）、アイコン PNG（758 幅用に再生成）
  build.sh          zig cc で arm 静的ビルド / 手元ビルド
  tests/            画面スナップショット（Mac で PNG を出してモックと差分）、favorites の解析
kindle/khome/       端末に置くもの（bin/khome、起動ラッパー、KUAL メニュー）
```

## 実装の段取り

1. 描画コア（canvas / text / layout / png_out）を Mac 上で完成させ、home / settings の PNG がモックと揃うところまで。端末不要。
2. 端末側（fb / input / lipc / own・release）と最小の HOME（描画・タップでブラウザ起動・取り返し）。SSH から手で起動して確認。
3. お気に入り、時計の毎分更新、復帰時の再描画、スリープ防止。
4. 新聞画面、設定画面。
5. KOReader / Rakuyomi 連携。
6. 安全装置と upstart 登録（了承を得てから）。

## 第 2 段階の実測メモ（2026-10-23）

- **own() は cvm も SIGSTOP する。** pillow を disable して awesome を止めても、cvm（booklet.home）が 1 分ごとにステータスバーの時計を描き続けた（65 秒で x=714..724, y=12..25 の 74 px が変化）。cvm を止めると 75 秒間 fb が 1 バイトも変わらなかった。release() で cvm を CONT してから appmgrd に次を投げる。
- own() の順序: タッチ EVIOCGRAB → cvm/awesome SIGSTOP → pillow `disableEnablePillow disable` → koreader.sh と同じ 11 サービスを `stop` → volumd SIGSTOP → 300ms 待つ → 全面描画 + `fbink -s -W GC16`。pillow の status bar が外れる再描画が非同期なので、この 300ms が要る。
- release() は逆順（サービス `start` → cvm/awesome/volumd CONT → pillow enable）。どちらも `owned` フラグで冪等。
- 取り返しのイベントは **`appStarted "com.lab126.booklet.home"`**（`appmgrd` を `lipc-wait-event -m -t` で購読）。実測では `go 0` → `appActivating 0` → 約 1.1 秒後に `appStarted` → `go 1`。fb を粗く採取した範囲では、`appStarted` の前後で純正ホームの絵に切り替わっていた。`appStarted` から **1200ms 待って** own() → 描画すると上書きされない（15 秒後の画面と差分 0 px を確認）。KOReader の子プロセス終了（SIGCHLD）も同じ 1200ms 待ちに合流させ、二重に来る appStarted はタイマーを引き直すだけにした。
- スリープ復帰は **`outOfScreenSaver`** だけを見る（短いスリープでは `wakeupFromSuspend` は来ない）。復帰後は cvm/awesome を SIGSTOP に戻し、**500ms 待って**全面再描画。復帰直後から 0.2 秒間隔で fb の上 41 行（y=0..40）を 5 秒採取すると、ハッシュが変わるのは再描画の 1 回だけで、その後は一定だった。再描画の 70 秒後も右上 x=700..758, y=0..40 は 0 px 変化（変わるのは khome 自身が描く時計だけ）。500ms で cvm の後描きは無い。
- `lipc-wait-event -m -t` の出力は `[HH:MM:SS.ffffff] event "arg" "arg" 0`。`*` 購読でも同じ形式（`-t` なしだと先頭のタイムスタンプが消えるだけ）。パーサは両方を受ける。
- 電池は `lipc-get-prop -i com.lab126.powerd battLevel`。Wi-Fi は `lipc-get-prop -s com.lab126.wifid cmState` が `CONNECTED` かどうか、SSID は `-s com.lab126.wifid feelingLuckyProfile`（`currentEssid` は 0x8 で取れない）。取れないときは `WIFI / ON`。
- **電源キーは event0（max77696-onkey）を EVIOCGRAB せずに開く。** `/proc/bus/input/devices` の `Name="max77696-onkey"` から引く（event 番号は決め打ちしない）。OWN 中に `KEY_POWER`(116) の押下を見たら、cvm と awesome を SIGCONT して powerd の `frameworkStarted` 問い合わせに答える。cvm だけだと powerd は `SCREEN SAVER` に遷移するが blanket の screenSaver ウィンドウは `IsUnMapped` のままで khome の画面が残った（実測）。awesome も CONT すると 2 秒以内に 758×1024 の screenSaver が `IsViewable` になる。SIGCONT からスクリーンセーバーが上に来るまで最大 1 秒ほど純正ホームが見える。
- **`goingToScreenSaver` では cvm/awesome を動かしたまま**（`ASLEEP`）。**`outOfScreenSaver` で両方 SIGSTOP → 500ms → 全面再描画**（上記）。**15 秒待っても `goingToScreenSaver` が来ない**ときは、powerd がボタンを拒否したとみなして両方 SIGSTOP に戻し、全面再描画する。待っている間は純正ホームが上に見える（実測）が、タイムアウトで khome に戻る。
- **`/dev/input/event0` への input_event 注入では powerd は反応しない。** 注入した `KEY_POWER` は khome など evdev の読み手には届くが、`/var/log/messages` に `Power button pressed` は出ず、powerd の state も `active` のまま。実機のボタンを押せない試験では、khome 側は event0 注入、powerd 側は `lipc-set-prop -i com.lab126.powerd powerButton 1` で代用した。cvm/awesome を CONT した状態ならこれが rc=0 で `ACTIVE → SCREEN SAVER` になり、復帰も同じコマンドで `ACTIVE` に戻る。cvm を SIGSTOP したままだと従来どおり `lipcErrTimedOut`（10 秒待ち）。
- 端末側の入口は `launcher/tools/deploy.sh`（`/mnt/us/khome/{bin,fonts,icons}` へ転送、フォントは sha256 が違うときだけ）と `kindle/khome/khome.sh`（PATH/DISPLAY/TZ、flock と pidof で二重起動防止、終了時に release 相当を必ず実行）。
- 終了時に純正画面へ戻す: khome は起動直後（own の前）の fb を `/var/tmp/khome-fb.dump` に保存し、OWN のまま終了するとき release() の 300ms 後に書き戻して GC16 で refresh する。`appmgrd start app://com.lab126.booklet.home` だけでは X 側にダメージが無く、ツールバー帯が khome の描いたまま残った（実測）。SIGKILL などで khome が書き戻せなかった場合はラッパーが同じファイルを書き戻す。
