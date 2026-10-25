# 実機調査: 純正ブラウザの全画面化・上端スワイプ・タブ

- 調査日: 2026-10-23
- 端末: Paperwhite 2（Pinot / Wario）、FW 5.12.2.1.1、`ssh kindle`（root、<kindle-ip>）
- 方針: **読み取りのみ**。`lipc-set-prop` / `kill` / `stop` / `initctl` / `mntroot rw` / ファイル変更 / 再起動は一切していない。LIPC は `lipc-get-prop` と `lipc-probe` のみ。
- 注意: 調査中に別作業者が純正 UI の停止・再開を行っており、`com.lab126.pillow` のプロパティ一覧が一時的に欠けた（後述）。画面状態の観測値は「その瞬間の値」。こちらからブラウザは一度も起動していない（他作業者が起動した可能性はあるが、観測していない）。
- 表記: **【事実】** = 実機で読んだ/見た、**【推測】** = コードからの推定、**【外部】** = 他機種・他 FW の公開情報、**【未検証】** = この個体で未確認。

---

## 0. 結論（先に）

1. **上端のステータスバー（時計・電池）は pillow が描く**。実体は `pillowd` がホストする WebKit の「pillow case」で、`/usr/share/webkit-1.0/pillow/default_status_bar.html` + JS/CSS。X ウィンドウ名は `L:C_N:titleBar_ID:system`（758×41+0+0）。awesome はこれを chrome レイヤに載せ、アプリの表示領域をその下にずらすだけで、描画はしない。
2. **ブラウザのツールバー（アドレスバー、戻る、進む、更新、メニュー）も pillow が描く HTML/JS**。`/usr/share/webkit-1.0/pillow/search_bar.html` + `search_bar.js` / `search_bar.css`。ブラウザ（Mesquite/WAF）は `com.lab126.chromebar` に `configureChrome` を送ってプロファイルとボタンを設定する。ブラウザ本体の `index.html` は空で、コンテンツは別の WebKit content window。
3. **純正ブラウザにタブ機能は無い**。コンテンツウィンドウは 1 個だけで、`target=_blank` 等の新規ウィンドウ要求は同じウィンドウへの遷移に丸められる。複数ページは「戻る/進む」1 本の履歴とブックマーク/履歴ビューで扱う。タブ一覧に相当する UI は無い。
4. **全画面化の最有力候補は、ブラウザの WAF 設定 `config.xml` に `<kindle:chrome><kindle:asset key="configureSearchBar" value="none"/></kindle:chrome>` を足すこと**。これは rootfs ではなく `/var/local/mesquite/browser/config.xml`（書き込み可能）で完結する。他機種の WAF で「`none` にすると WAF ウィンドウがステータスバー帯まで含めて全画面を覆った」という実績がある【外部】。この個体では未検証。
5. **ツールバーの表示/非表示は LIPC で可能**。`com.lab126.winmgr chromeState`（rw Int）を 1/0 にする。`com.lab126.pillow displayChrome` / `dismissChrome` はそれぞれ `chromeState=1/0` を書くラッパー（逆アセンブルで確認）。**ただし chromeState はウィンドウの z オーダーを変えるだけで、アプリのジオメトリは変えない**。PC:TS のまま隠すと上に 116px の空白帯が残る。PC:N（全画面）と組み合わせたときだけ「全画面 + 一時的なツールバー」になる。
6. **上端スワイプの検出は、外部プロセスが `/dev/input/event1` を grab せずに読む方式が第一候補**。検出したら `chromeState=1` を送る。evdev は複数リーダーを許すので、ブラウザにもタッチは届く（＝ページもスクロールする）。副作用を嫌うなら `EVIOCGRAB` + `/dev/input/uinput` 再注入のブローカーにする。`/dev/input/uinput` は存在する。
7. **ブラウザ表示中に最上部 40px に khome の表示が残る件**は、既定のブラウザが PC:TS で y=116 から始まる（推定）ため、top 41px は pillow の titleBar しか描かないことと整合する。pillow の復帰が遅い/無効な間に khome が直接 `/dev/fb0` に書いた画素が残る。PC:N にしても、e-ink では「隠した chrome が更新時に透ける」「X が khome の直接 fb 書き込みを知らない」という別の残像問題がある（他機種の実例あり）。全画面化後は強制フルリフレッシュと、非表示時の titleBar の unmap/縮小が必要。

---

## 1. ブラウザ画面の構成

### 1.1 上端のステータスバー（時計・電池）

**【事実】** ホーム表示中の `xwininfo -root -tree`（2026-10-23、ホームが前面のとき）:

```text
0x1200040 "L:C_N:titleBar_ID:system_RC:true_KIWI:CVM": ()  758x41+0+0    MapState=IsViewable
0x1200090 "L:C_N:searchBar_ID:system_RC:true_KIWI:CVM": () 758x75+0+41   MapState=IsViewable
0x12000c4 "L:A_N:application_ID:com.lab126.booklet.home_M:false_PC:TS_RC:true_WT:true_ASR:true_O:U...": () 758x908+0+116
```

- `titleBar`（ロール `N:titleBar`、レイヤ `L:C`）が上端 41px。`searchBar`（`N:searchBar`）がその下 75px。アプリ（home）は `PC:TS` で y=116 から 908px。
- この 41px / 75px は固定定数ではなく、各 pillow case の HTML/CSS の高さで決まる:
  - `/usr/share/webkit-1.0/pillow/stylesheets/default_status_bar.css:10` — `height: 12.96pt;`（212dpi で約 38px、実測ウィンドウは 41px）
  - `/usr/share/webkit-1.0/pillow/stylesheets/search_bar.css:22` — `height: 25.44pt;`（約 75px）
- `default_status_bar.html:17` の body が `onclick="State.appState.tapHandler('titleBar')"`。`default_status_bar.js:20` で `tapHandler: nativeBridge.raiseChrome`。つまりステータスバーをタップすると chrome を上げる。
- 時計は `default_status_bar.js:475-486` の `setTimeout(setTime, 60000 - ...)`、電池は `default_status_bar_battery.js` が DOM を更新する。
- ウィンドウのロール名は `window_title.js:64` の `WINMGR.ROLE.TITLEBAR = 'titleBar'`。

**【事実】** awesome 側は描画せず、配置とレイヤだけを担当する:

- `/etc/xdg/awesome/lab126_chrome_layer.lua:364-404` — top bar を `x=0, y=offset` に置き、T → S → A の順に積む。高さは `clientGeometry.height`（ウィンドウの実寸）。
- `/etc/xdg/awesome/lab126_chrome_layer.lua:762-773` — `chrome_get_persistent_top_offset(PC)` が `PC` に応じて titleBar / searchBar の高さを返す。
- `/etc/xdg/awesome/lab126_application_layer.lua:28-55` — アプリの `y = topOffset`、`height = screenHeight - (topOffset + bottomOffset)`。`topOffset` は上記関数の戻り値（`:36`）。
- `/etc/xdg/awesome/rc.lua:41` で `lab126_chrome_layer` を require。`rc.lua:100-101` に `Shift+C`/`Shift+X` で `chrome_raise`/`chrome_lower` するデバッグキーがある。

**結論**: ステータスバーは **pillow が描き、awesome が場所とレイヤを決める**。描画の主体は pillow だが、khome の実測コメント（`launcher/src/main.c:224-227`）では、awesome と pillow を止めても cvm が 1 分ごとに右上の時計領域（x 714..724, y 12..25）を描き直すとある。**時計の更新契機の一部は cvm 側にもある**とみられる【推測】。

### 1.2 ブラウザ自身のツールバー（アドレスバー、戻る、メニュー）

**【事実】** ブラウザは Mesquite/WAF アプリで、`/var/local/mesquite/browser/` を `-c` で読む（`/var/local/appreg.db` の `command = /usr/bin/mesquite -l com.lab126.browser -c file:///var/local/mesquite/browser/ -j`）。

- `/var/local/mesquite/browser/index.html:70-71` — `<body></body>` は空。UI はここには無い。
- `/var/local/mesquite/browser/index.html:52` — `javascripts/chrome.js` を読み込む。
- `/var/local/mesquite/browser/javascripts/chrome.js:172-202` — `applicationManager` の `load` で、
  - `b.navigationBar.initialize(profileName, ...)`（`:174`）
  - `b.contentWindow.get()`（`:177`）
  - `pillow.buttons.config = buttonConfig`（`:186`、back/forward/refresh の 3 ボタン + menu）
  - `pillow.search.domainsConfig = customDomains`（`:198`）
  - `pillow.configure(browserAppId, profileName)`（`:202`、`browserAppId='com.lab126.browser'`, `profileName='browser'`）
- `/var/local/mesquite/browser/javascripts/chrome.js:205-229` — menu ボタンでメニュー表示。
- `/var/local/mesquite/browser/javascripts/chrome.js:232-250` — back/forward/refresh のハンドラ。
- `/var/local/mesquite/shared/javascripts/pillow.js:40` — `pillowId = 'com.lab126.chromebar'`
- `/var/local/mesquite/shared/javascripts/pillow.js:47` — `pillowAction = 'configureChrome'`
- `/var/local/mesquite/shared/javascripts/pillow.js:187-195` — `kindle.messaging.sendMessage('com.lab126.chromebar', 'configureChrome', message)`
- `/var/local/mesquite/shared/javascripts/pillow.js:340-368` — `pillow.configure(id, name)`。`profile.name`, `buttons`, `searchProviders`, `searchState` を送る。

**【事実】** 実際のツールバー HTML は `/usr/share/webkit-1.0/pillow/search_bar.html`:

- `:35` `<body>`
- `:38` back、`:40` forward、`:45` search、`:72` refresh、`:79` menu
- `:53-75` アドレス/検索入力（`wide-search`、`input-container`、`input#input`）
- `:15` `stylesheets/search_bar.css`、`:28` `javascripts/search_bar.js`

ツールバーのウィンドウは `L:C_N:searchBar_ID:system`。アプリ固有ではなく **system の search bar** で、ブラウザはその clientParams（プロファイル・ボタン・URL 表示・進捗）だけを差し替える。

**整理**:

| 要素 | 描画主体 | 実体 | ファイル |
|---|---|---|---|
| 上端ステータスバー（時計・電池・Wi-Fi） | pillowd（WebKit） | `titleBar` pillow case | `/usr/share/webkit-1.0/pillow/default_status_bar.html`、`javascripts/default_status_bar*.js`、`stylesheets/default_status_bar.css` |
| ブラウザのツールバー（URL、戻る、進む、更新、メニュー） | pillowd（WebKit） | `searchBar` pillow case | `/usr/share/webkit-1.0/pillow/search_bar.html`、`javascripts/search_bar*.js`、`stylesheets/search_bar.css` |
| ページ本体 | mesquite（WAF） | アプリウィンドウ + WebKit content window | `/var/local/mesquite/browser/javascripts/browser.js`、`shared/javascripts/contentWindow.js` |
| ウィンドウ配置・レイヤ | awesome | Lua | `/etc/xdg/awesome/lab126_chrome_layer.lua`、`lab126_application_layer.lua` |

「chrome は HTML/JS かネイティブか」の答え: **見た目は pillowd がホストする HTML/JS**。ただしウィンドウ生成・レイヤ・表示/非表示・ジオメトリは **ネイティブ（libpillow.so + awesome）** が握る。ブラウザ側の `kindle.chrome` API はネイティブブリッジで、`setTitleBar` / `createContentWindow` / `hide` などを持つ。

---

## 2. タブ機能の有無

**【事実】** 無い。

- `/var/local/mesquite/browser/javascripts/browser.js:28` — `var contentWindow = b.contentWindow.get();` が 1 個だけ。
- `/var/local/mesquite/browser/javascripts/browser.js:31` — `contentWindow.addEventListener('openWindow', onOpenWindow)`。
- `/var/local/mesquite/browser/javascripts/browser.js:151-161` — `onOpenWindow(url)` は `file:///` / `widget://` / `about: blank` 以外の新規ウィンドウ要求を `b.navigator.go(url)` で**同じウィンドウ**に流す。タブは作らない。
- `/var/local/mesquite/browser/javascripts/browser.js:232-250` — back/forward/refresh は 1 本の履歴スタック。
- `/var/local/mesquite/browser/javascripts/menuItems.js:19-52` — メニューは Bookmarks / Bookmark Page / History / Settings / Article Mode のみ。タブ一覧は無い。
- `/var/local/mesquite/browser/javascripts/browser.js:98-140` — 起動 state は `view`（URL）、`test`（テストスクリプト）、`disableJS` だけ。タブを復元する state は無い。

**複数ページの扱い**: 1 本の戻る/進む履歴、`lastUrl`、ブックマーク（`bookmarks`）、履歴（`history`）ビュー。`/mnt/us/.active_content_sandbox/browser/resource/LocalStorage` の localStorage に保存される（device-survey.md 参照）。「タブ」に相当するものは、せいぜい「履歴一覧から選び直す」まで。

**タブ UI を作る場合**: 純正ブラウザの範囲では不可能。WAF アプリを自作して複数の content window（または iframe）を管理するか、khome 側で URL リストを持ち、選択時に `app://com.lab126.browser?view=...` で開き直す「疑似タブ」にするしかない。後者はページ状態（フォーム入力、スクロール位置）を保持しない。

---

## 3. 全画面化の候補と必要な変更

### 3.1 ブラウザの設定 / LIPC プロパティ

**【事実】** `com.lab126.browser` は通常の LIPC サービスではない。

```text
$ lipc-probe -v com.lab126.browser
（出力なし、exit 1）
```

- `/var/local/appreg.db` の `com.lab126.browser` の properties は `command` / `framework` / `unloadPolicy` の 3 つだけ。`lipcId` は無い。
- ブラウザの設定は `localStorage` の `settings`（`javascriptDisabled`, `imagesDisabled`, `orientation`）だけ（`device-survey.md` の 1.3）。フルスクリーン/ツールバー非表示の設定は無い。
- 起動 state のキーは `view` / `test` / `disableJS` のみ（`browser.js:98-130`）。
- **したがって、`lipc-set-prop com.lab126.browser ...` で全画面にする公式プロパティは無い**【事実】。ブラウザ起動中に `-l com.lab126.browser` で Mesquite が LIPC 名を登録する可能性はあるが、今回ブラウザを起動していないので未確認【未検証】。

**【事実】** ブラウザの WAF 設定 `/var/local/mesquite/browser/config.xml`:

- `:6` `viewmodes="application"`
- `:37-42` `<kindle:gestures>` は `drag`, `pinchzoom`, `tap`, `multi_tap`, `hold`。`swipe` は無い。
- `:77-80` `<kindle:messaging>` に `com.lab126.pillow` と `com.lab126.chromebar`。
- **`<kindle:chrome>` 要素が無い**。これが他アプリとの違い。

**【事実】** 他の WAF アプリは `<kindle:chrome>` でシステム chrome を制御している。例:

- `/var/local/mesquite/audibleupsell/config.xml:64-67`
- `/var/local/mesquite/grokterms/config.xml:45-48`
- `/var/local/mesquite/ku/config.xml:25-28`
- `/var/local/mesquite/mfa/config.xml:58-61`
- `/var/local/mesquite/odac/config.xml:53-56`
- `/var/local/mesquite/prime/config.xml:56-59`
- `/var/local/mesquite/privacy/config.xml:56-59`
- いずれも `<!-- Disable system chrome -->` のコメント付きで `<kindle:asset key="configureSearchBar" value="none" />`。
- 逆に `store` と `webview` は `value="system"`（`/var/local/mesquite/store/config.xml:65-67`、`/var/local/mesquite/webview/config.xml:65-67`）。

**【外部】** `zampierilucas/kindle-button-mapper-rs` の commit `eccb16b` は、WAF アプリで `configureSearchBar` を `none` にすると **WAF ウィンドウがステータスバー帯まで含めて全画面を覆った**、と記録している（同時に、隠れたステータスバーが widget 更新時に透ける問題も記録）。URL は §9。この機種・FW は異なるが、Mesquite/WAF の同じ設定項目を使っている。

**【推測】** ブラウザの `config.xml` に次を足せば、ブラウザの WAF ウィンドウが `PC:N`（全画面、top offset 0）になり、ステータスバーと検索バーは「非永続 chrome」としてウィンドウの背後に回る（`chromeState=0` の間は見えない）。ブラウザ自身が `pillow.configure` で searchBar を設定しているので、`chromeState=1` で後からオーバーレイ表示できる、という筋が立つ。

```xml
<kindle:chrome>
    <kindle:asset key="configureSearchBar" value="none" />
</kindle:chrome>
```

- 変更場所: `/var/local/mesquite/browser/config.xml`（**rootfs ではない**。`/var/local` は mmcblk0p3 の書き込み可能パーティション）。
- ただし、この個体のブラウザで `PC:N` になるか、searchBar が残るか、ブラウザ JS が問題なく動くかは **未検証**。
- 注意: `configureSearchBar=none` は「search bar を永続 chrome から外す」設定であり、searchBar ウィンドウ自体を破棄するかは実装依存。`chromeState=1` で searchBar が出てこない可能性もある。要実測。

**【未検証】** `viewmodes="fullscreen"` に変える案。Mesquite の文字列には `fullscreen` / `windowed` / `maximized` があるが、主要な WAF アプリの設定はすべて `viewmodes="application"`（`/var/local/mesquite/*/config.xml` を確認。例外は `/var/local/mesquite/features/jquery/config.xml:5` の `windowed` のみ）。`viewmodes` は「サポートするモードの列挙」であり、現在のモードを直接指定するものではない可能性が高い。**第一候補にしない**。

**【推測/未検証】** `/var/local/appreg.db` の `com.lab126.browser` に `default-chrome-style=NH` を足す案。Kindle の Java ブックレット（リーダー、KUAL）はこのプロパティを持ち、`/opt/amazon/ebook/lib/kafui.jar` の `KindleFrameFactory$PersistentChromeMode` には `NO_HEADER` / `TITLE_ONLY` / `TITLE_AND_SEARCH` がある。ただしこれは Java フレームワーク側の経路で、Mesquite/WAF のブラウザが読む保証は無い。**効果不明**。

### 3.2 pillow のステータスバーを LIPC で隠せるか

**【事実】** `com.lab126.pillow` のプロパティ（UI が正常なときの全リスト）:

```text
w  Str  disableEnablePillow
w  Str  dismissChrome
w  Str  customDialog
w  Str  applicationWindow
w  Str  activityIndicator
w  Str  debugInfo
w  Str  displayChrome
w  Str  interrogatePillow
rw Has  interrogatePillowHash  [*NOT SHOWN*]
w  Str  pillowAlert
rw Str  logLevel
rw Str  logMask
```

（調査中、別作業者が UI を止めたタイミングでは `disableEnablePillow` / `dismissChrome` / `interrogatePillow` 等が一時的に消えていた。上のリストは復帰後の値。）

**【事実】** `dismissChrome` / `displayChrome` の実体は `com.lab126.winmgr chromeState` への 0/1 書き込み:

- `libpillow.so` の `utilsDismissChrome`（0xc92c）は `lipc_setIntProperty("com.lab126.winmgr", "chromeState", 0)` を呼ぶ。
- `utilsDisplayChrome`（0xc960）は同じく `chromeState` に 1 を書く。
- つまり `lipc-set-prop com.lab126.pillow dismissChrome ...` と `lipc-set-prop com.lab126.winmgr chromeState 0` はほぼ同義。

**【事実】** `chromeState` は awesome の `lab126_chrome_layer.lua:680-696` で `rw Int` として登録され、1 で `chrome_raise()`、0 で `chrome_lower()` を呼ぶ。`chrome_raise()` は `:148-179`、`chrome_lower()` は `:184-217`。chrome ウィンドウを CHROME レイヤに上げ下げするだけで、**アプリウィンドウのジオメトリは変更しない**。アプリの位置は `lab126_application_layer.lua:28-55` が `PC` から計算する。

- **PC:TS のまま `chromeState=0`** → 上端 116px は chrome が消えて空白（`blankBackground`）になる。全画面にはならない。
- **PC:N のとき `chromeState=1`** → 全画面のページの上に titleBar + searchBar がオーバーレイされる。`chromeState=0` で隠すとページは全画面のまま。これが望みの挙動。
- `chrome_raise()` は `:167` で `chromeLayer.activeBars["S"].win.c` にフォーカスを当てる。searchBar が存在しないと Lua エラーになり得るので、PC:N でも system searchBar が生きていることが前提。

**【外部】** MobileRead の JBPatch スレッド（post #196）: `lipc-set-prop -i com.lab126.winmgr chromeState 1` で chrome を上げるが、直後に 0 に戻ることがある。`lipc-get-prop -i com.lab126.winmgr chromeState` で確認し、0 なら再設定する、とある。実装ではリトライが必要。

**【事実】** `disableEnablePillow`:

- `/mnt/us/khome` の khome は画面を借りる間 `lipc-set-prop com.lab126.pillow disableEnablePillow disable` を実行し、終了時に `enable` に戻す（`launcher/src/main.c:230`, `:257`）。
- KOReader も FW >= 5.6.5 で同じコマンドを使う（`/mnt/us/koreader/koreader.sh:255-260`）。
- ただし KOReader のコメント: 「FW 5.6.5 以降は soft hide/unhide が正しく効かない」「FW 5.7.2 以降はこれだけでは時計の再描画を止められないので awesome を SIGSTOP する」（`koreader.sh:255-275`）。
- `disableEnablePillow` は pillow 全体を無効化するので、ブラウザの searchBar も消える。**全画面化の手段としては過剰**で、ツールバーを後から出す用途には向かない。

**【事実】** `interrogatePillow`:

- `libpillow.so` に `gtimercb_interrogatePillow` / `interrogatePillowHash` があり、`{"pillowId": "...", "function": "..."}` を受けて pillow case の JS を実行する。
- KOReader は FW < 5.6.5 の経路で `lipc-set-prop com.lab126.pillow interrogatePillow '{"pillowId": "default_status_bar", "function": "nativeBridge.hideMe();"}'` を使う（`koreader.sh:278-281`）。復帰は `showMe()`（`:389-394`）。
- これは **ステータスバーだけ**を隠す。searchBar は残る。FW 5.12.2.1.1 では KOReader 自身がこの経路を使っていない（soft hide が効かないため）。**この個体での実効性は未検証**。

**【事実】** 他アプリの全画面方法:

- **Kindle リーダー**: `/var/local/appreg.db` の `com.lab126.booklet.reader` は `default-chrome-style=NH`、`searchbar-mode=transient` を持つ。`/opt/amazon/ebook/lib/kafui.jar` の `KindleFrameFactory$PersistentChromeMode` には `NO_HEADER` / `TITLE_ONLY` / `TITLE_AND_SEARCH` がある。`NH` が `NO_HEADER` に対応するなら、リーダーは「永続 chrome なし（全画面）+ 検索バーは一時的」をフレームワークに要求している【推測。対応表は未確認】。
- **KOReader**: フレームワークを止めない既定モードでは、`disableEnablePillow disable` → `SIGSTOP awesome` → （FW < 5.12.4 なら）`wmctrl` で `:titleBar_ID:` の高さを 1px に縮小 → 終了時に復元（`koreader.sh:255-282`, `:369-412`）。`--framework_stop` なら `stop lab126_gui` で GUI ごと止める（`:217-229`, `:359-367`）。
- KOReader は「ステータスバーを隠す」ために pillow を無効化し、awesome を止め、titleBar を縮小している。**純正ステータスバーは簡単には消えない**ことの実例。interactive-spikes.md でも、KOReader 表示中に時計・電池が右上に残ったと記録されている（`docs/spikes/interactive-spikes.md:51`）。

### 3.3 awesome のウィンドウ配置（タイトル/ステータス領域の高さ）

**【事実】** 設定ファイル:

- `/etc/xdg/awesome/rc.lua` — awesome のエントリ。`lab126_chrome_layer` を require（`:41`）。
- `/etc/xdg/awesome/lab126_chrome_layer.lua` — top bar の積み上げ（`:364-404`）、`chromeState` プロパティ（`:680-696`）、`chrome_get_persistent_top_offset`（`:762-773`）。
- `/etc/xdg/awesome/lab126_application_layer.lua` — アプリの y/height（`:28-55`、`topOffset` は `:36`）。
- **高さの定数ファイルは無い**。titleBar 41px / searchBar 75px は pillow case の CSS（`default_status_bar.css:10`, `search_bar.css:22`）とウィンドウの実寸で決まり、awesome はその実寸を読む。

**【事実】** awesome は `lxinit` から引数なしで起動される（`/etc/upstart/x.conf` の `exec lxinit blanket ...`、`ps` の `awesome`）。`/proc/3045/environ` は `HOME=/tmp/root`, `PWD=/`。`/mnt/us` 側の設定ファイルを読む仕組みは無い。`/etc/xdg/awesome/rc.lua` を差し替えるには **rootfs の書き換え（`mntroot rw`）が必要**。

**【事実】** `setPreference` は `/var/local/system/winmgr_preference.prop` に key=value を書くだけで（`lab126_preference.lua:6`, `:49-60`）、高さや chrome 表示には使えない。

**ランタイムで高さを変える方法（rootfs 不要）**: KOReader 同梱の `wmctrl` で `:titleBar_ID:` の高さを 1px にする、という回避策がある（`koreader.sh:266-272`, `:397-412`）。awesome は chrome ウィンドウの `updatedGeometry` を受けて top bar を再配置し、アプリの `topOffset` を再計算する。ただし KOReader のコメントには「ligl のタイムアウトを誘発することがある」「FW 5.12.4 以降では復帰時に UI がソフトロックする報告がある」とある（`:263-265`）。この個体は 5.12.2.1.1（5.12.4 未満）なので後者は該当しないが、前者のリスクは残る。

### 3.4 chrome の HTML/JS を /mnt/us から上書き・差し替えできるか

**【事実】** 二種類の「chrome」を分ける必要がある。

1. **ブラウザ WAF アプリ本体の HTML/JS** — `/var/local/mesquite/browser/`（`config.xml`, `index.html`, `javascripts/`, `stylesheets/`）。`/var/local` は書き込み可能で、**rootfs ではない**。ここを編集すればブラウザの JS 挙動は変えられる。ただし **ウィンドウの `PC`（永続 chrome の種類）はネイティブ側が決める**ので、JS だけで全画面にはできない。
2. **pillow の chrome バー HTML/JS** — `/usr/share/webkit-1.0/pillow/`（`default_status_bar.html`, `search_bar.html`, JS, CSS）。**rootfs 上**。`libpillow.so` の `pillowCreateWindow` は `/usr/share/webkit-1.0/pillow/` + `<name>.html` を組み立て、ファイルが無ければ `/usr/local/tests/pillow/` を見る（`libpillow.so` 内の文字列 0x18dfc, 0x18e1a, 0x18e62）。**`/mnt/us` を見る設定は無い**【事実】。

**【事実】** `/usr/share/webkit-1.0/pillow` を `/mnt/us` で上書きする仕組み（overlay, bind mount, 環境変数）は見つからない。`config.xml` にも pillow case のパスを変える項目は無い。**差し替えには rootfs の書き換えが必要**。

**【事実】** `com.lab126.chromebar` の `configureChrome` は、既存バーの `clientParams`（プロファイル、ボタン、検索文字列、進捗）を実行時に更新できる。HTML 自体を差し替える API ではない。PW6（FW 5.19.6）の `topNavBar` / `template: blank` は新しい Chromium ブラウザ世代のスキーマで、5.12 の `searchBar.clientParams` スキーマとは別物（`onepointfive-REAL/kindle-browser-stuff`）。**この個体にはそのまま適用できない**【外部】。

**【事実】** `test=` state を使うと `b.tester.execute(testScriptSource)` がブラウザのアプリ文書に `<script>` を注入し、`window.kindleApp` にブラウザの sandbox を露出する（`/var/local/mesquite/shared/javascripts/tester.js:45-57`, `browser.js:119-121`）。ただしこれは起動時 1 回のテスト用で、chrome の HTML を差し替えるものではない。恒久的な UI 変更には向かない。

**【事実】** appreg の `command` は `-c file:///var/local/mesquite/browser/` を指す。`/mnt/us` のコピーを読ませるには、appreg の `command` を変えるか、`mesquite -l ... -c file:///mnt/us/...` を直接起動して別アプリとして登録する必要がある。どちらも rootfs ではないが、純正ブラウザの差し替えではなく「別アプリ」になる。

---

## 4. 「上からスワイプでツールバー／タブを出す」の候補

### 4.1 ブラウザ自身のジェスチャ機能

**【事実】** 無い。

- `config.xml:37-42` のジェスチャは `drag`, `pinchzoom`, `tap`, `multi_tap`, `hold`。`swipe` / `flick` は有効化されていない（Mesquite の設定パーサは `swipe` / `flick` を受け付ける: `libmesquiteconfig.so` の文字列 0x21a62, 0x21a57）。
- ブラウザ JS の `swipe` はページスクロール用（`/var/local/mesquite/shared/javascripts/views.js:311-357`）。chrome を出す処理は無い。
- ステータスバーをタップすると `nativeBridge.raiseChrome` が呼ばれる（`default_status_bar.js:20`）が、これは **chrome が表示されているとき**だけ。chrome が隠れているとタップは届かない。
- awesome 側にもスワイプ/エッジジェスチャの処理は無い（`/etc/xdg/awesome` を `swipe|gesture|edge` で検索して該当なし）。

### 4.2 ツールバー表示切替の LIPC

**【事実】** ある。

- `lipc-set-prop com.lab126.winmgr chromeState 1` → `chrome_raise()`。titleBar と searchBar を CHROME レイヤに上げる。
- `lipc-set-prop com.lab126.winmgr chromeState 0` → `chrome_lower()`。
- `lipc-set-prop com.lab126.pillow displayChrome ...` / `dismissChrome ...` は同じことをするラッパー（`libpillow.so` の `utilsDisplayChrome` / `utilsDismissChrome`）。
- `com.lab126.winmgr focusChrome` は特定の chrome ウィンドウにフォーカスするだけで、表示はしない。
- **PC:N と組み合わせるのが前提**。PC:TS だと chrome を隠してもアプリは y=116 のままで、上に空白が残る。

### 4.3 外部プロセスが `/dev/input/event1` を grab せずに読む

**【事実】** evdev は同じデバイスを複数プロセスで open/read できる。`EVIOCGRAB` をしなければ、他のプロセス（ブラウザ/X）にもイベントは届く。

- khome は電源キー `/dev/input/event0` を **grab せずに** open している。コメントに「No EVIOCGRAB: powerd has to see the same KEY_POWER.」とある（`launcher/src/input.c:150`）。
- タッチ `/dev/input/event1` は、khome が画面を所有する間だけ `input_grab(&touch, 1)` で grab する（`launcher/src/main.c:223`, `input.c:173-186`）。ブラウザ表示中は khome は release しているので grab は外れている。
- したがって、ブラウザ表示中に別プロセスが `open("/dev/input/event1", O_RDONLY|O_NONBLOCK)` して MT イベントを読むことは可能。`ABS_MT_POSITION_X/Y` と `ABS_MT_TRACKING_ID` / `BTN_TOUCH` を追えば、上端（y < 40 程度）から下方向へのスワイプを検出できる。
- **副作用**: そのタッチはブラウザのページにも届く。上端から下へスワイプすると、ページが上方向にスクロールしようとする。ページが最上部ならオーバースクロールで済むが、途中ならページが動く。まずは「小さな上端領域（y < 16〜20）」「短時間（< 500ms）」「下方向 60px 以上」に限定して試すのが現実的。

### 4.4 外部プロセスが grab + uinput で再注入する

**【事実】** `/dev/input/uinput` は存在する（`crw-r----- root root 10, 223`）。`/dev/uinput` は無い。

- タッチブローカーが `/dev/input/event1` を `EVIOCGRAB` で独占し、全イベントを読む。
- 上端スワイプと判定したら消費し、それ以外は `/dev/input/uinput` で作った仮想タッチデバイスにそのまま再注入する。
- これならページにスワイプが届かない。ただし、すべてのタッチを中継するので、バグるとタッチが効かなくなる。実装・検証コストは高い。**第 2 段階の案**。

### 4.5 X11 経由で観測する

**【事実】** Xorg のログには `XInputExtension` と `XTEST` の初期化はあるが、`RECORD` の初期化は見当たらない（`/var/log/Xorg.0.log`）。`xdpyinfo` は入っていない。

- XRecord でタッチを観測する案は、この X サーバでは使えない可能性が高い【推測】。
- XInput2 の raw event を root window で選択して観測できる可能性はあるが、未検証。XTEST は注入専用で観測には使えない。
- evdev を読む方が確実で実装も小さい。

### 4.6 ブラウザ JS / content window を使う

**【事実】** ブラウザの `contentWindow` は `.window`（DOM window）を公開している（`/var/local/mesquite/shared/javascripts/contentWindow.js:23-24`, `:376-382`）。アプリ JS から `contentWindow.window.document.addEventListener('touchstart', ...)` のようにリスナーを付けられる可能性がある。

- ただし、mesquite の `kindle.chrome` ネイティブブリッジにあるのは `hide` / `setTitleBar` / `createContentWindow` / `createDialog` などで、**`raiseChrome` は見当たらない**（`mesquite` の `JSObjectChrome` 文字列。`raiseChrome` は libpillow の pillow case 側 `nativeBridge` のメソッド）。JS から chrome を上げる API は見当たらない。
- `tester.js` の `test=` 注入はアプリ文書に対して 1 回だけ。恒久ジェスチャには向かない。
- したがって、JS だけで「上スワイプでツールバー」を完結させるのは難しい。**検出は JS、表示は外部 LIPC** の組み合わせなら可能だが、外部プロセスを置くなら最初から evdev で検出する方が単純。

---

## 5. ブラウザ表示中に最上部 40px に khome の表示が残る件

### 5.1 観測されている構造

**【事実】** khome は画面を所有する間、`/dev/fb0` を mmap して直接描く（`launcher/src/fb.c:16`, `:59`, `:83-84`）。X のウィンドウではない。

- khome のステータステキストは設計座標 y=56、フォント 24（`launcher/src/layout.c:136-176`）。デバイススケールは 758/1072（`main.c:36`）なので、`56 * 758/1072 ≒ 39.6px`。**「最上部 40px 程度」と一致する**。
- khome はブラウザ起動時に `release()` してから `appmgrd start` する（`launcher/src/state.c:30-36`, `main.c:242-258`, `:377-385`）。`release()` は `disableEnablePillow enable`、cvm/awesome の SIGCONT、サービスの start を行うが、**描画の完了は待たない**。
- 逆に `own()` は「pillowd がステータスバーを非同期に unmap するので、X が露出部分を再描画するまで 300ms 待つ」と明示的に待つ（`main.c:235-237`）。**release 側にはこの待ちが無い**。

**【事実】** ホーム時のウィンドウ: titleBar 758×41+0+0、searchBar 758×75+0+41、home アプリ 758×908+0+116（`xwininfo`）。stock titleBar の高さ 41px は、khome のステータス文字の位置（約 40px）とほぼ同じ帯。

**【推測】** ブラウザの既定ウィンドウは `PC:TS`（home と同じ）で、y=116 から 908px。PC:TS ならブラウザのコンテンツは y=0..40 を描かない。top 41px を描くのは pillow の titleBar だけ。したがって、ブラウザ起動直後に pillow の titleBar がまだ復帰していない/描き直していないと、khome が最後に fb0 に書いたステータス画素がそのまま見える。これが「40px 残る」の最も自然な説明。

- ブラウザが `PC:TS` であることは未観測（ブラウザを起動していないため）。ただし、ブラウザの config に `<kindle:chrome>` が無く、`pillow.configure` で searchBar を使うことから、`PC:TS` と推定する。
- **ブラウザのウィンドウは X の通常クライアント**であり、自分の矩形の外は描かない。PC:TS なら top 116px は矩形外。PC:N なら矩形は画面全体（0..1024）になるが、それは「X のウィンドウが全面」という意味で、e-ink のパネルが全面リフレッシュされる保証ではない（5.2）。
- 確認方法: ブラウザ起動中に `xwininfo -root -tree` で `L:A...com.lab126.browser...PC:?` とジオメトリを見る。`PC:TS` なら `758x908+0+116`、`PC:N` なら `758x1024+0+0` になるはず。

### 5.2 全画面にした場合の残像

**【外部】** `zampierilucas/kindle-button-mapper-rs` の commit `eccb16b`:

> With `configureSearchBar=none` the WAF window covered the whole screen including the status bar strip, so the clock, wifi and battery only appeared when a widget value changed.

つまり、WAF ウィンドウが全画面を覆っても、**隠れているステータスバーが更新されると、その領域だけ e-ink に透けて出る**。これは Kindle の e-ink 更新が X の z オーダーを完全には尊重しないため、と解釈できる【推測】。

**【事実】** khome のコメント:

> The stock UI tracks damage in X and never learns that khome wrote straight to /dev/fb0, so asking appmgrd to restart booklet.home leaves parts of our frame behind.
> （`launcher/src/main.c:261-264`）

これは「X のダメージ管理は fb0 への直接書き込みを知らない」という、まさに残像の原因を述べている。

**【推測】** PC:N にするとブラウザのウィンドウは 0..1024 を覆うので、マップ/ロード時に X は全面をダメージとして扱い、fb0 上はブラウザの画素で上書きされるはず。しかし、以下の 2 つが残る:

1. **e-ink の部分更新**: X が全面ダメージを出しても、ligl が実際にパネルをフルリフレッシュしない限り、古い khome 画素が残る可能性がある。全画面化直後に `fbink -s -W GC16` などでフルリフレッシュするのが安全。
2. **隠した titleBar の更新**: `chromeState=0` で titleBar を隠しても、その WebKit の時計タイマー（`default_status_bar.js:475-486`）は動き続ける。更新のたびに top 41px が透ける可能性がある（上記の外部報告）。対策は、titleBar を unmap する（`interrogatePillow` の `hideMe`）か、`wmctrl` で高さ 1px / 画面外に縮小するか、cvm/awesome を止める。

### 5.3 確認手順（次回、書き込みが許されるとき）

1. ブラウザを起動し、`xwininfo -root -tree | grep -E "com.lab126.browser|titleBar|searchBar"` で `PC` とジオメトリを見る。
2. `lipc-get-prop com.lab126.winmgr chromeState` を確認。
3. `cat /dev/fb0` を PGM に変換し、top 41px の画素を khome の既知のステータス画素と比較する（`scripts/dev/kshot.sh` が使える）。
4. ブラウザ表示中に 2〜3 分待ち、時計の分替わりで top 41px が変化するかを見る（隠れた titleBar の透け）。
5. PC:N の状態で `chromeState=1` → 0 を繰り返し、searchBar がオーバーレイ表示/非表示になるか、ページのジオメトリが変わらないかを確認する。

---

## 6. 実現案の分類表

### 6.1 全画面化

| 案 | 変更場所 | rootfs 書換 | 効果 | 状態 |
|---|---|---|---|---|
| ブラウザ config に `configureSearchBar=none` を追加 | `/var/local/mesquite/browser/config.xml` | **不要** | WAF ウィンドウが全画面（PC:N）になる見込み。ツールバーは `chromeState=1` でオーバーレイ | 【外部】他機種で実績。この個体では未検証。**第一候補** |
| ブラウザ config の `viewmodes` を `fullscreen` に変更 | 同上 | **不要** | 不明。`viewmodes` はサポートモードの列挙で、現在モード指定ではない可能性 | 【未検証】低優先 |
| appreg に `default-chrome-style=NH` を追加 | `/var/local/appreg.db` | **不要** | Java ブックレット用の経路。Mesquite が読む保証なし | 【推測】効果不明 |
| `chromeState=0` / `dismissChrome` | LIPC（実行時） | **不要** | chrome を隠すが、アプリは y=116 のまま。上に空白帯 | 【事実】単独では全画面にならない |
| `disableEnablePillow disable` | LIPC（実行時） | **不要** | pillow 全体を無効化。searchBar も消える。時計の再描画は止まらない（KOReader の実績） | 【事実】過剰。単独では全画面にならない |
| `interrogatePillow` で `default_status_bar` を `hideMe` | LIPC（実行時） | **不要** | ステータスバーだけ隠す。searchBar は残る。FW 5.12 では soft hide が不確実 | 【未検証】補助用 |
| `wmctrl` で titleBar / searchBar の高さを 1px に | 実行時（`/mnt/us` の wmctrl） | **不要** | awesome がアプリの topOffset を再計算し、ほぼ全画面になる。chrome を出すときは高さを戻す必要 | 【事実】KOReader が同種の手法。ligl タイムアウトリスク |
| awesome の Lua を編集して topOffset を 0 に | `/etc/xdg/awesome/lab126_application_layer.lua` 等 | **必要** | 全アプリに影響。条件分岐を入れればブラウザだけ 0 にできる | 【事実】rootfs 書換が必要 |
| pillow の `default_status_bar.css` / `search_bar.css` を編集 | `/usr/share/webkit-1.0/pillow/stylesheets/` | **必要** | バーの高さを 0/1px にできる。全アプリに影響 | 【事実】rootfs 書換が必要 |
| pillow の HTML/JS を差し替え | `/usr/share/webkit-1.0/pillow/` | **必要** | chrome の見た目・挙動を自由にできる。`/mnt/us` overlay は無い | 【事実】rootfs 書換が必要 |
| カスタム WAF アプリを `/mnt/us` に置き、appreg に登録 | `/mnt/us/...` + `/var/local/appreg.db` | **不要** | 純正ブラウザではないが、最初から全画面・独自ツールバー・タブも可能 | 【事実】実装大 |
| `lipc-set-prop com.lab126.browser ...` | — | — | そのようなプロパティは無い | **無理** |

### 6.2 上端スワイプでツールバー表示

| 案 | 仕組み | rootfs 書換 | 副作用 / リスク | 状態 |
|---|---|---|---|---|
| ブラウザ標準ジェスチャ | なし（`drag`/`pinchzoom`/`tap` のみ） | — | — | **無理** |
| 外部プロセスが event1 を非 grab で読み、`chromeState=1` | MT イベントを監視。上端 y<40 から下方向 60px で発火 | **不要** | ページもスクロールする。実装は小さい | 【事実】可能。第一候補 |
| 外部プロセスが event1 を grab + uinput 再注入 | 全タッチを中継し、上端スワイプだけ消費 | **不要** | 実装大。バグるとタッチ全損。`/dev/input/uinput` は存在 | 【事実】可能。第 2 段階 |
| XRecord で観測 | X の Record 拡張 | — | Xorg ログに RECORD 初期化なし | 【推測】不可 |
| XInput2 raw event | root window で XI2 を選択 | **不要** | 未検証。X サーバの XI2 対応次第 | 【未検証】 |
| ブラウザ JS で検出 | `contentWindow.window` に touch リスナー | **不要** | `kindle.chrome` に raise が無い。表示は外部 LIPC が必要 | 【推測】検出のみ可能 |
| `interrogatePillow` で `showMe` | ステータスバーのみ表示 | **不要** | searchBar は出ない。ツールバーにならない | 【事実】不十分 |

### 6.3 タブ

| 案 | 実現性 |
|---|---|
| 純正ブラウザのタブ機能 | **無理**（存在しない） |
| khome が URL リストを持ち、選択でブラウザを開き直す | 可能（疑似タブ）。ページ状態は保持されない |
| カスタム WAF アプリで複数 content window / iframe | 可能だが実装大。純正ブラウザの差し替えになる |
| 純正ブラウザの JS を改造してタブ UI | ウィンドウ/コンテンツが 1 個しかないため、複数ページの同時保持は難しい。**非現実的** |

---

## 7. 推奨案

### 7.1 第一候補（rootfs を書き換えない）

1. **ブラウザ config の変更（要バックアップ）**
   - `/var/local/mesquite/browser/config.xml` に `<kindle:chrome><kindle:asset key="configureSearchBar" value="none"/></kindle:chrome>` を追加する。
   - 変更前に `config.xml` を `/mnt/us/khome/backup/` などにコピーする。
   - 起動して `xwininfo` で `L:A...com.lab126.browser...PC:N` と `758x1024+0+0` を確認する。
   - `chromeState=1` で searchBar がページ上に出るか、`0` で消えるかを確認する。
   - ブラウザのページ読み込み、戻る/進む、メニューが壊れていないか確認する。

2. **ツールバー表示デーモン（khome の仲間として）**
   - `/dev/input/event1` を `O_RDONLY|O_NONBLOCK` で開く（**EVIOCGRAB しない**）。
   - `ABS_MT_TRACKING_ID` / `BTN_TOUCH` でジェスチャ開始・終了を判定。開始 y < 40、下方向 60px 以上、500ms 以内で「上端スワイプ」。
   - `lipc-set-prop com.lab126.winmgr chromeState 1` を送る。`lipc-get-prop` で 0 のままなら 1〜2 回リトライ（MobileRead の報告）。
   - 10 秒後に `chromeState 0` を送る。ツールバー表示中に再度上端スワイプしたらタイマーをリセット。
   - まずは非 grab で試し、ページのスクロールが気になるなら uinput ブローカーに移行する。

3. **残像対策**
   - 全画面化直後に `fbink -s -W GC16` などでフルリフレッシュする（khome が既に使っている FBInk）。
   - 非表示時の titleBar の透けが気になるなら、`interrogatePillow` の `hideMe` を試す。効かなければ `wmctrl` で titleBar の高さを 1px にする（KOReader と同様）。ツールバーを出す直前に元の高さへ戻す。
   - cvm の時計再描画が問題になるなら、khome と同様に cvm を SIGSTOP する（ブラウザは Mesquite なので home booklet の cvm を止めてもページ表示は続くはず）。ただし home に戻すときに SIGCONT が必要。

### 7.2 第二候補（第一候補が失敗した場合）

- `wmctrl` で titleBar / searchBar の高さを 1px にし、awesome にアプリを再配置させる。rootfs 不要だが、chrome を出すたびに高さを戻す必要があり、ligl タイムアウトリスクがある。
- それも不安定なら、`/mnt/us` にカスタム WAF アプリを置き、`viewmodes` / `<kindle:chrome>` を最初から全画面向けに設定して、appreg に別アプリとして登録する。純正ブラウザの page rendering は Mesquite の content window をそのまま使えるが、URL バー・履歴・メニューは自前になる。
- どうしても純正ブラウザのまま rootfs を触らずに安定させたいなら、**ステータスバー 41px を常に表示する**（＝完全全画面は諦める）のが最も安全。`chromeState=0` で searchBar だけ隠し、titleBar は残す、という折衷もある。

### 7.3 タブについて

- 純正ブラウザにタブは無い。**タブ一覧を出すなら、khome 側の URL リスト（疑似タブ）か、カスタム WAF アプリ**になる。
- 疑似タブは「ページ状態を保持しない」「戻る履歴はブラウザ側の 1 本」という制約を受け入れるなら、実装は比較的小さい。khome が `app://com.lab126.browser?view=<encodeURIComponent(url)>` で開き直すだけ。

---

## 8. 未確認・リスク

- **ブラウザを起動していない**。`com.lab126.browser` の LIPC サービス、実際の `PC`、ウィンドウジオメトリ、`configureSearchBar=none` の効果は未観測。
- **`configureSearchBar=none` の外部実績は他機種・他 FW**（`zampierilucas/kindle-button-mapper-rs`）。5.12.2.1.1 の Mesquite で同じ結果になるかは未検証。
- **`chromeState=1` で searchBar が出るか**は、`configureSearchBar=none` が searchBar ウィンドウを破棄するかどうかに依存する。ブラウザが `pillow.configure` を送るので残る見込みだが、未検証。
- **`config.xml` の変更は OTA で上書きされる可能性**がある。バックアップと再適用の仕組みが必要。
- **soft hide（`interrogatePillow` の `hideMe`）は FW 5.6.5 以降で正しく効かない**という KOReader の知見がある。`disableEnablePillow` も単独では時計の再描画を止められない。無理に使わない。
- **`wmctrl` での titleBar 縮小は ligl タイムアウトを誘発する**と KOReader がコメントしている。この FW は 5.12.4 未満なので「復帰時ソフトロック」の報告には該当しないが、リスクは残る。
- **uinput ブローカーはタッチ全損のリスク**。最初は非 grab で。
- **e-ink の残像 / 隠れた chrome の透け**は、PC:N にしても完全には解決しない可能性がある。フルリフレッシュと titleBar の unmap/縮小を組み合わせて実測する必要がある。
- **`viewmodes="fullscreen"` は未検証**。主要な WAF 設定は `application` なので、期待しない方がよい。
- **`default-chrome-style=NH` は Java ブックレット用**。Mesquite のブラウザには効かない可能性が高い。
- **ブラウザの `unloadPolicy=unloadOnPause`**（`/var/local/appreg.db`）。別アプリが前面に出るとブラウザがアンロードされ、ページ状態が失われる可能性がある。キオスク用途では、khome 側で `activeApp` を監視してブラウザを再起動する必要がある。
- 調査中に別作業者が UI を停止/再開したため、`lipc-probe` のプロパティ一覧が一時的に変わった。最終的なリストは復帰後のもの。画面の観測値もその瞬間のもの。

---

## 9. 根拠ファイル・出典

### 実機ファイル

- `/var/local/appreg.db` — `com.lab126.browser` の command / unloadPolicy、`com.lab126.booklet.reader` の `default-chrome-style=NH` / `searchbar-mode=transient`。
- `/var/local/mesquite/browser/config.xml:6` — `viewmodes="application"`。`:37-42` gestures。`:77-80` messaging。`<kindle:chrome>` なし。
- `/var/local/mesquite/browser/index.html:52,67,70-71` — chrome.js / browser.js、空 body。
- `/var/local/mesquite/browser/javascripts/chrome.js:159-165,172-202,205-250` — titleBar 更新、pillow 設定、ボタン。
- `/var/local/mesquite/browser/javascripts/browser.js:28,98-140,151-161,232-250` — contentWindow 1 個、onGo の state、新規ウィンドウの丸め、履歴。
- `/var/local/mesquite/browser/javascripts/menuItems.js:19-52` — Bookmarks / Bookmark Page / History / Settings。
- `/var/local/mesquite/shared/javascripts/pillow.js:40,47,187-195,340-368` — `com.lab126.chromebar` / `configureChrome`。
- `/var/local/mesquite/shared/javascripts/contentWindow.js:23-24,376-382,650` — contentWindow の `.window` と `kindle.chrome.createContentWindow`。
- `/var/local/mesquite/shared/javascripts/tester.js:45-57` — `test=` 注入と `window.kindleApp`。
- `/usr/share/webkit-1.0/pillow/default_status_bar.html:17,35-57` — ステータスバー DOM（時計・電池）。
- `/usr/share/webkit-1.0/pillow/javascripts/default_status_bar.js:20,475-486,982-986` — `raiseChrome`、時計タイマー、ウィンドウサイズ。
- `/usr/share/webkit-1.0/pillow/javascripts/default_status_bar_battery.js:5-6` — 電池状態。
- `/usr/share/webkit-1.0/pillow/javascripts/window_title.js:64` — `TITLEBAR` ロール。
- `/usr/share/webkit-1.0/pillow/search_bar.html:35-79` — ツールバー DOM（back/forward/refresh/menu/URL）。
- `/usr/share/webkit-1.0/pillow/stylesheets/default_status_bar.css:10` — `height: 12.96pt`。
- `/usr/share/webkit-1.0/pillow/stylesheets/search_bar.css:22` — `height: 25.44pt`。
- `/etc/xdg/awesome/rc.lua:41,100-101` — chrome layer の require、デバッグキー。
- `/etc/xdg/awesome/lab126_chrome_layer.lua:83-120,148-179,184-217,364-404,680-696,762-773` — chrome のレイヤ/上げ下げ/配置/`chromeState`/top offset。
- `/etc/xdg/awesome/lab126_application_layer.lua:28-55` — アプリの y/height。
- `/etc/xdg/awesome/lab126_preference.lua:6,49-60` — `setPreference` はファイル保存のみ。
- `/etc/upstart/x.conf` — `exec lxinit blanket ...`（awesome の起動）。
- `/mnt/us/koreader/koreader.sh:255-282,359-412` — pillow 無効化、awesome SIGSTOP、wmctrl titleBar 縮小、復帰。
- `launcher/src/main.c:36,216-258,261-264,377-385` — khome の own/release、fb0 直接描画、ブラウザ起動。
- `launcher/src/state.c:30-36` — release → start browser。
- `launcher/src/input.c:134-160,173-186` — event0 を grab せず open、event1 の EVIOCGRAB。
- `launcher/src/fb.c:16,59,83-84` — `/dev/fb0` mmap。
- `launcher/src/layout.c:136-176,608-622` — ステータスバー文字、時計矩形。
- `libpillow.so`（`/usr/lib/libpillow.so.1.0`）:
  - `utilsDismissChrome` 0xc92c — `lipc_setIntProperty("com.lab126.winmgr","chromeState",0)`
  - `utilsDisplayChrome` 0xc960 — 同 1
  - `pillowCreateWindow` 0x7a78 — `/usr/share/webkit-1.0/pillow/` + `.html`、fallback `/usr/local/tests/pillow/`
  - `gtimercb_interrogatePillow` 0xa620 / `gtimercb_interrogatePillowHash` 0x9e40
- `/opt/amazon/ebook/lib/kafui.jar` — `KindleFrameFactory$PersistentChromeMode`: `NO_HEADER`, `TITLE_ONLY`, `TITLE_AND_SEARCH`。
- `/opt/amazon/ebook/lib/ReaderSDK-impl.jar` — `ChromeBarController`, `ChromeBarReaderView`（リーダーの chrome 制御）。
- `/opt/amazon/ebook/lib/app_utilities.jar` — `ChromeUtils`（`ChromeHeaderBar$Style` の get/set）。
- `/opt/amazon/ebook/lib/displaycontroller.jar` — `ChromeSearchBarUtils`。

### 実機で実行した確認コマンド（読み取りのみ）

```text
lipc-probe -v com.lab126.browser        # 出力なし、exit 1（未起動）
lipc-probe -v com.lab126.pillow         # dismissChrome/displayChrome/interrogatePillow 等
lipc-probe -v com.lab126.chromebar      # configureChrome/searchResults/activityIndicator
lipc-probe -v com.lab126.winmgr         # chromeState/focusChrome 等
lipc-get-prop com.lab126.winmgr chromeState   # 0
xwininfo -root -tree                    # titleBar 758x41+0+0, searchBar 758x75+0+41, home PC:TS 758x908+0+116
ls -l /dev/input/uinput                 # crw-r----- root root 10, 223
grep -i -E "record|xtest|XI" /var/log/Xorg.0.log  # XInputExtension/XTEST、RECORD なし
```

### 外部出典

- Kindle Modding Wiki, `com.lab126.pillow` — プロパティ一覧（`disableEnablePillow`, `dismissChrome`, `displayChrome`, `interrogatePillow` 等）: <https://kindlemodding.org/kindle-apps-and-services/com.lab126.pillow.html>
- Kindle Modding Wiki, `com.lab126.winmgr` — `chromeState` / `focusChrome` 等: <https://kindlemodding.org/kindle-apps-and-services/com.lab126.winmgr.html>
- Kindle Modding Wiki, `com.lab126.chromebar` — `configureChrome` 等: <https://kindlemodding.org/kindle-apps-and-services/com.lab126.chromebar.html>
- MobileRead, JBPatch thread page 14, post #196（eureka）— `chromeState 1` で chrome を上げる、0 に戻ることがあるので `lipc-get-prop` で確認して再設定: <https://www.mobileread.com/forums/showthread.php?t=175512&page=14>
- `zampierilucas/kindle-button-mapper-rs`, commit `eccb16b913529c68c54fb5f9dd7641db49769c13` — `configureSearchBar=none` で WAF ウィンドウがステータスバー帯まで全画面を覆う、隠れたステータスバーが widget 更新時に透ける: <https://github.com/zampierilucas/kindle-button-mapper-rs/commit/eccb16b913529c68c54fb5f9dd7641db49769c13>
- `onepointfive-REAL/kindle-browser-stuff` — PW6/FW 5.19.6 の Chromium ブラウザで `com.lab126.chromebar configureChrome` の `topNavBar` を `blank` にして URL バーを隠す研究。5.12 の Mesquite とはスキーマが違う点に注意: <https://github.com/onepointfive-REAL/kindle-browser-stuff>
- KOReader `platform/kindle/koreader.sh` — pillow 無効化、awesome SIGSTOP、wmctrl titleBar 縮小、復帰: <https://github.com/koreader/koreader/blob/master/platform/kindle/koreader.sh>
- MobileRead, `p=2449275`（KOReader が参照する titleBar 縮小の元ネタ）: <https://www.mobileread.com/forums/showpost.php?p=2449275&postcount=5>
- MobileRead, `p=2454141`（`interrogatePillow` で `default_status_bar` を隠す元ネタ）: <https://www.mobileread.com/forums/showpost.php?p=2454141&postcount=34>
