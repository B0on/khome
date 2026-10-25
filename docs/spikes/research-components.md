STATUS: DONE
RESULT:

## 結論（先に）

- **Tachiyomi の Kindle ネイティブ版は存在しません。** 現行の Mihon は Android 8.0 以上専用で、Kindle は Android ではありません。現実的な代替は、① RakuYomi を Kindle 上で動かす、② Suwayomi サーバー + `suwayomi.koplugin`、③ Suwayomi の OPDS を KOReader で読む、の3系統です。([Mihon README](https://github.com/mihonapp/mihon), [Kindle FAQ](https://kindlemodding.org/jailbreaking/jailbreak-faq.html))
- **PW4 / FW 5.12.2.1.1 は soft-float（armel）です。** KOReader は `koreader-kindlepw2-v2026.07.1.zip`、RakuYomi は `rakuyomi-kindle.zip` を使います。`kindlehf` は FW 5.16.3 以降用なので、この FW では入れないでください。([KOReader Kindle wiki](https://github.com/koreader/koreader/wiki/Installation-on-Kindle-devices), [RakuYomi build 表](https://github.com/tachibana-shin/rakuyomi/blob/main/docs/src/user-guide/installation/choosing-a-build.md), [soft/hard float の説明](https://kindlemodding.org/jailbreaking/jailbreak-faq.html))
- **ブラウザは純正 Experimental Browser しか実用的な選択肢がありません。** 5.12 系は Mesquite 上の WebKit 533 系で、現代の SPA・動画・複雑な CSS はかなり厳しいです。KOReader に汎用ブラウザはなく、Kindle 用の Chromium/WebKit 移植も見つかりませんでした。([Mesquite の制約](https://github.com/penguins184/Penguins-Kindle-Wiki/blob/main/Mesquite-WAFs.md))
- **ホーム画面は段階を踏むべきです。** まず WAF/Illusion のアイコングリッドを KUAL から起動して UX を固める。起動時から出すなら OpenReader 型の upstart 置換が唯一の具体例ですが、20★・リリースなし・最終更新 2025-10 のプロトタイプです。PW4 は secure boot でパーティション復元ができないため、いきなり boot 置換しない方が安全です。([OpenReader](https://github.com/terpinedream/OpenReader), [PW4 がバックアップ対象外の理由](https://kindlemodding.org/jailbreaking/Legacy/backing-up/backing-up.html))
- **おすすめの現実解**は、ランチャー（自作 C + FBInk、または WAF）＋ KOReader（RakuYomi または Suwayomi plugin）＋ ブラウザは「サーバー側でレンダリングしたニュース/RSS/リーダー」に寄せる構成です。純正ブラウザで任意の現代サイトを快適に、は 5.12 では期待しないでください。

---

## 1. Tachiyomi の Kindle 版・代替

### 1.1 RakuYomi（本家は archive、現行は tachibana-shin fork）

- 本家 `hanatsumi/rakuyomi` は **2026-01-05 に archive** されました。README も「もう積極的にはメンテしない。tachibana-shin の fork を見ろ」と明記しています。本家の最終リリースは **v1.22.2（2025-06-22）** です。([本家 GitHub](https://github.com/hanatsumi/rakuyomi), [本家 releases](https://github.com/hanatsumi/rakuyomi/releases))
- 現行の公式扱いは **`tachibana-shin/rakuyomi`** です。2026-10-23 時点の最新リリースは **v1.44.1（2026-10-19）**、640★、open issues 6、AGPL-3.0。README は Aidoku 全般に加え、**LNReader / Mangayomi / Tachiyomi・Mihon（Keiyoushi）拡張**も扱うとしています。([fork GitHub](https://github.com/tachibana-shin/rakuyomi), [v1.44.1](https://github.com/tachibana-shin/rakuyomi/releases/tag/v1.44.1))

#### PW4（FW 5.12.2.1.1）で使うビルド

| 用途 | ファイル | 対象 |
|---|---|---|
| KOReader 本体 | `koreader-kindlepw2-v2026.07.1.zip` | PW2 以降のタッチ機種、FW ≤ 5.16.2.1.1。PW4 はこれ。([wiki](https://github.com/koreader/koreader/wiki/Installation-on-Kindle-devices)) |
| RakuYomi | `rakuyomi-kindle.zip` | FW < 5.16.3 向け。arm-unknown-linux-musleabi の soft-float。([build 表](https://github.com/tachibana-shin/rakuyomi/blob/main/docs/src/user-guide/installation/choosing-a-build.md)) |
| RakuYomi HF | `rakuyomi-kindlehf.zip` | FW ≥ 5.16.3 向け。**5.12 では使わない。** |
| RakuYomi A9 | `rakuyomi-kindlea9.zip` | Cortex-A9 最適化。公式表では PW3 以前・Voyage・Kindle 7・Oasis 1 が対象で、**PW4 は明記されていない**。 |
| KOReader HF | `koreader-kindlehf-v2026.07.1.zip` | FW ≥ 5.16.3 向け。**5.12 では使わない。** |

補足：

- PW4 の SoC は **NXP i.MX6 SoloLite（Cortex-A9 系、32-bit ARMv7）** です。したがって `kindlea9` が理屈上は動く可能性がありますが、RakuYomi の公式表は PW4 を A9 対象に含めていません。まずは無印 `kindle` で動作確認し、速さに不満がある場合だけ自己責任で A9 を試すのが保守的です。([PW4 の CPU](https://en.wikipedia.org/wiki/Amazon_Kindle), [i.MX6 と Cortex-A9](https://en.wikipedia.org/wiki/I.MX))
- RakuYomi の Kindle 向けバイナリは **静的 musl** で、Kindle に musl の動的リンカや .so がない前提のビルドです。`kindle` / `kindlehf` / `kindlea9` は readelf で動的依存と INTERP がないことを検査してから zip 化されています。([build-all.sh](https://github.com/tachibana-shin/rakuyomi/blob/main/scripts/build-all.sh))

#### インストール手順（KOReader 既存前提）

1. KOReader を `koreader-kindlepw2-v2026.07.1.zip` で更新／導入する。USB ルートに `koreader/` と `extensions/` ができる状態にする。([KOReader wiki](https://github.com/koreader/koreader/wiki/Installation-on-Kindle-devices))
2. `rakuyomi-kindle.zip` を展開し、中の **`rakuyomi.koplugin` フォルダ**を `/mnt/us/koreader/plugins/` にコピーする。([インストール docs](https://github.com/tachibana-shin/rakuyomi/blob/main/docs/src/user-guide/installation/installing-to-your-device.md))
3. `/mnt/us/koreader/rakuyomi/settings.json` を用意する。初回起動時に自動生成もされますが、先に書いてもよいです。([同 docs](https://github.com/tachibana-shin/rakuyomi/blob/main/docs/src/user-guide/installation/installing-to-your-device.md))
4. KOReader を再起動し、File Manager の上メニュー → **Search → Rakuyomi** を開く。([Quickstart](https://github.com/tachibana-shin/rakuyomi/blob/main/docs/src/user-guide/quickstart.md))
5. RakuYomi の左上メニュー → **Manage Sources** → **+** でソース一覧を開き、使うソースをタップしてインストールする。([Quickstart](https://github.com/tachibana-shin/rakuyomi/blob/main/docs/src/user-guide/quickstart.md))

`settings.json` の例（日本語ソースを出す場合）：

```json
{
  "source_lists": [
    { "url": "https://tachibana-shin.github.io/aidoku-sources-next/index.min.json", "type": "aidoku" },
    { "url": "https://aidoku-community.github.io/sources/index.min.json", "type": "aidoku" },
    { "url": "https://github.com/keiyoushi/extensions/raw/repo/index.pb", "type": "keiyoushi" }
  ],
  "languages": ["ja", "en"]
}
```

- `type` は省略すると `aidoku` 扱いです。`keiyoushi` は Mihon/Tachiyomi の `index.pb` プロトコルです。([settings.schema.json](https://github.com/tachibana-shin/rakuyomi/releases/latest/download/settings.schema.json))
- `languages` は「利用可能ソース一覧のフィルタ」と、ソースに渡すグローバル設定の両方に使われます。日本語だけにするなら `["ja"]`、英語も残すなら `["ja","en"]`。([settings.schema.json](https://github.com/tachibana-shin/rakuyomi/releases/latest/download/settings.schema.json))

#### 日本語ソースの有無

**Aidoku コミュニティの既定インデックス**（2026-10-23 時点で 140 ソース）に含まれる日本語ソースは、次の4つでした。

- webアクション — `https://comic-action.com`
- コミックDAYS — `https://comic-days.com`
- ニコニコ漫画 — `https://manga.nicovideo.jp`
- 少年ジャンプ＋ — `https://shonenjumpplus.com`

出典：`https://aidoku-community.github.io/sources/index.min.json` を取得して `languages` に `ja` を含むものを抽出（2026-10-23）。

**Keiyoushi（Mihon/Tachiyomi）インデックス**はもっと多く、2026-10-23 時点で **1,420 拡張・179 日本語ソース**が登録されています。主な例：

- 少年ジャンプ＋ / Shonen Jump+ — `https://shonenjumpplus.com`
- コミックDAYS / Comic Days — `https://comic-days.com`
- カドコミ / KadoComi — `https://comic-walker.com`
- マガジンポケット / Magazine Pocket — `https://pocket.shonenmagazine.com`
- サンデーうぇぶり / Sunday Web Every — `https://www.sunday-webry.com`
- ピッコマ / Piccoma — `https://piccoma.com`
- LINEマンガ / Line Manga — `https://manga.line.me`
- ニコニコ漫画 / Nicovideo Seiga — `https://sp.manga.nicovideo.jp`
- BookLive — `https://booklive.jp`
- BookWalker Japan — `https://bookwalker.jp`

出典：`https://github.com/keiyoushi/extensions/raw/repo/index.pb` の元データ `index.json` を取得し、`sources[].language == "ja"` を抽出（2026-10-23）。Keiyoushi のリポジトリは [こちら](https://github.com/keiyoushi/extensions)。

注意：

- Keiyoushi には NSFW・海賊版系も多数含まれます。`contentWarning` を見て、公式配信・出版社系だけを使うのが安全です。法的・規約上の責任は利用者側です。
- ログインが必要なソース、Cloudflare の 403 が出るソースがあります。RakuYomi fork には **Telegram の Cookie Sync Bot** で Cookie を同期する仕組みがあります。([README](https://github.com/tachibana-shin/rakuyomi))
- DRM 付きの購入コンテンツは期待しないでください。RakuYomi が扱えるのは、ソースが返す画像ページ／ダウンロードです。
- Keiyoushi 拡張は APK のまま `dexvm` で動かす実装です（`backend/shared/Cargo.toml` の `dexvm = { features = ["keiyoushi"] }`）。PW4 のような低メモリ機では、Rust サーバー + dexvm + KOReader の同時実行が重くなります。fork は「RAM を食い過ぎて KOReader がフリーズするのを避ける」「メモリを解放する cleaner」を追加したと README に書いていますが、PW4 実機での軽快さは**未確認**です。([README](https://github.com/tachibana-shin/rakuyomi), [Cargo.toml](https://github.com/tachibana-shin/rakuyomi/blob/main/backend/shared/Cargo.toml))

### 1.2 Suwayomi サーバー + KOReader プラグイン / OPDS

**サーバー**：`Suwayomi-Server` は Mihon/Tachiyomi 拡張を実行する自宅サーバーで、2026-10-23 時点の最新は **v2.4.2366（2026-09-30）**。Java 21 の jar、Docker、Windows/macOS/Linux バンドルがあります。既定ポートは **4567**。OPDS と OPDS-PSE をサポートし、エンドポイントは **`/api/opds/v1.2`** です。([GitHub](https://github.com/Suwayomi/Suwayomi-Server), [README](https://github.com/Suwayomi/Suwayomi-Server#readme), [v2.4.2366](https://github.com/Suwayomi/Suwayomi-Server/releases/tag/v2.4.2366))

**KOReader 専用プラグイン**：`LK4D4/suwayomi.koplugin` が現行です。2026-10-23 時点の最新は **v1.4.0（2026-10-07）**、20★。Kindle を含む KOReader 端末で動き、サーバーのライブラリ閲覧、ソース検索、拡張のインストール、既読状態の同期、**CBZ のローカルダウンロードとオフライン閲覧**ができます。([GitHub](https://github.com/LK4D4/suwayomi.koplugin), [v1.4.0](https://github.com/LK4D4/suwayomi.koplugin/releases/tag/v1.4.0))

インストール：

1. Suwayomi サーバーを PC/NAS/Docker で起動する。
2. Suwayomi の WebUI で Keiyoushi などの拡張リポジトリを追加し、使う日本語ソースをインストールする。最近の Suwayomi は既定の拡張リポジトリを含まない場合があるため、WebUI の Extensions 設定を確認する。([KoManga README の Suwayomi 手順](https://github.com/mateusz464/KoManga))
3. `suwayomi.koplugin-v1.4.0.zip` を展開し、`suwayomi.koplugin` を `/mnt/us/koreader/plugins/` に置く。([README](https://github.com/LK4D4/suwayomi.koplugin#install))
4. KOReader を再起動し、**Search → Suwayomi** を開く。サーバー URL、認証方式（Basic Auth / Simple Login / UI Login）を入れて **Test connection**。ダウンロード先フォルダを選ぶ。([README](https://github.com/LK4D4/suwayomi.koplugin#connect))

OPDS だけで済ませる場合：

- KOReader の内蔵 OPDS クライアントで `http://<server>:4567/api/opds/v1.2` を登録する。KOReader の OPDS は「オンラインカタログから本をダウンロードする」機能で、Suwayomi の OPDS フィードを読めます。([KOReader User Guide の OPDS](https://koreader.rocks/user_guide/), [Suwayomi の OPDS 実装](https://github.com/Suwayomi/Suwayomi-Server/blob/master/server/src/main/kotlin/suwayomi/tachidesk/opds/OpdsAPI.kt))

比較：

| 方式 | 動作場所 | 長所 | 短所 |
|---|---|---|---|
| RakuYomi fork | Kindle 単体 | PC 不要。Aidoku/Keiyoushi/LNReader/Mangayomi。オフライン可 | Kindle の RAM/CPU を食う。Keiyoushi は dexvm で重い。PW4 実機の快適さは未確認 |
| Suwayomi + `suwayomi.koplugin` | サーバー + Kindle | Tachiyomi 拡張資産がそのまま使える。CBZ オフライン。既読同期。端末負荷が軽い | 常時動く PC/NAS が必要。ネットワーク依存 |
| Suwayomi OPDS | サーバー + Kindle | KOReader 標準機能だけで済む。設定が単純 | プラグインより機能が少ない。ダウンロード管理・同期は弱い |
| ネイティブ Tachiyomi/Mihon | — | — | **Kindle 版は存在しない**。Mihon は Android 8+ |

### 1.3 その他の候補

- **KoManga**：Tachiyomi/Mihon ソースを Suwayomi 経由で使う自宅サーバー型。ただし README は **Kobo 専用**と明記しており、Kindle 対応は未確認。0★で 2026-06 作成の早期プロジェクトなので、現時点では候補外です。([KoManga](https://github.com/mateusz464/KoManga))
- **Tachiyomi の Kindle 移植**：見つかりませんでした。Kindle FAQ も「Android アプリを入れたいなら、拡張/ポートがない限り無理」と明言しています。([Kindle FAQ](https://kindlemodding.org/jailbreaking/jailbreak-faq.html))
- **Suwayomi の OPDS を KOReader で読む**：プラグインを入れられない／入れたくない場合のフォールバックとして有効です。

### 1.4 KOReader の最新版と PW4 のパッケージ

- 最新安定版は **v2026.07.1（2026-08-01 公開）**。([release](https://github.com/koreader/koreader/releases/tag/v2026.07.1))
- PW4 / FW 5.12.2.1.1 は **`koreader-kindlepw2-v2026.07.1.zip`**。KOReader wiki の表でも「PW2 以降のタッチ機種、FW ≤ 5.16.2.1.1」が `kindlepw2` です。([wiki](https://github.com/koreader/koreader/wiki/Installation-on-Kindle-devices))
- `kindlehf` は FW ≥ 5.16.3、`kindle` は K4/KT/PW1、`kindle-legacy` は物理キーボード機です。PW4 では使いません。

---

## 2. Kindle で使えるブラウザ

### 2.1 純正 Experimental Browser（FW 5.12 系）

#### エンジンと制約

- **FW 5.16.4 未満の Experimental Browser は Mesquite ベースで、Chromium ではありません。** 5.12.2.1.1 はこの範囲です。([Penguins の Mesquite 解説](https://github.com/penguins184/Penguins-Kindle-Wiki/blob/main/Mesquite-WAFs.md))
- Mesquite の WebKit は **Safari 5.0 相当 / WebKit 533.16 前後**。ES5 まで、`const`/`let`/arrow function/`Promise`/`fetch`/Flexbox/Grid/CSS variables はほぼ使えません。([Illusion Guide](https://github.com/penguins184/Penguins-Kindle-Wiki/blob/main/Illusion-Guide.md), [Mesquite の制約](https://github.com/penguins184/Penguins-Kindle-Wiki/blob/main/Mesquite-WAFs.md))
- したがって、現代の SPA・動画サイト・複雑な日本語ニュースサイトは表示崩れ／JS エラー／TLS 失敗が起きやすいです。**どこまで実用かはサイト依存で未確認**ですが、「スマホ風ブラウザ」として期待するのは危険です。

#### 起動方法（LIPC / appmgrd）

ブラウザの基本的な起動はこれです：

```sh
lipc-set-prop com.lab126.appmgrd start app://com.lab126.browser
```

- `com.lab126.appmgrd` の `start` プロパティに `app://...` を渡すと、`/var/local/appreg.db` の登録内容に従ってアプリを起動します。([KindleModding appmgrd](https://kindlemodding.org/kindle-apps-and-services/com.lab126.appmgrd.html), [CPR-Zero の説明](https://cpr-zero.checkpoint.com/vulns/cprid-2181/))
- ただし、これは「ブラウザを開く」ところまでで、**URL を引数で渡せるかは FW 依存**です。

#### 特定 URL を直接開けるか

- **PW6 / FW 5.19.6 では**、次の形式で URL を渡せることが実機確認されています：

```sh
lipc-set-prop com.lab126.appmgrd start "app://com.lab126.browser#going?url=https://example.com"
```

- これは 2026 年の実験リポジトリの記録で、**PW4 / FW 5.12.2.1.1 で同じ `#going?url=` が通るかは未確認**です。([kindle-browser-stuff](https://github.com/onepointfive-REAL/kindle-browser-stuff))
- 5.12 で確実なのは「ブラウザを開く」まで。URL 指定の代替は、
  - `/mnt/us/...` にリダイレクト用 HTML を置き、`<meta http-equiv="refresh" content="0;url=...">` や JS の `location.href` で飛ばす。
  - 後述の WebLaunch 型 WAF のように、URL を設定ファイルに書いた WAF を起動する。
  - ブラウザのブックマークを開いてタップする。
  のいずれかになります。
- 5.12 は 5.16.4 未満なので `file://` が使える可能性が高いです。新しい FW では `file://` が制限され、`KindleBrowserPatch` で復活させる必要があります。([KindleBrowserPatch](https://github.com/PiperKev/KindleBrowserPatch))

#### ブックマークの保存場所と形式

- 古い Kindle（Kindle 4 で確認）では **`/var/local/java/prefs/browser/bookmarks_wv`**。形式は **Java シリアライズされた `java.util.ArrayList<com.amazon.ebook.booklet.browser.BookmarkItem>`** で、各要素は `title` と `url` の2つの String です。編集後は再起動が必要です。([kindle-bookmark-editor](https://git.sr.ht/~sotirisp/kindle-bookmark-editor))
- PW5 / FW 5.18.1 以降では `bookmarks_wv` はなく、**`/var/local/KPPBrowser.db`** という SQLite DB に移っています。([MobileRead の PW5 スレッド](https://www.mobileread.com/forums/showthread.php?t=373851))
- **PW4 / 5.12.2.1.1 で `bookmarks_wv` か `KPPBrowser.db` かは実機未確認**です。5.12 は後者に移る前の世代なので `bookmarks_wv` の可能性が高い、という推測です。実機では `find /var/local -iname '*bookmark*'` で確認してください。

### 2.2 代替ブラウザ／WebKit・Chromium 移植

- **KOReader に汎用ブラウザはありません。** KOReader は OPDS、News Downloader（RSS/Atom を HTML 化）、HTML ファイルの閲覧はできますが、URL バー付きのブラウザではありません。([KOReader User Guide](https://koreader.rocks/user_guide/))
- **Kindle 用の Chromium/WebKit 単体移植は見つかりませんでした。** 検索で出てくる “kindle-web-browser” 系の多くは、Kindle の純正ブラウザで表示するためのサーバー側ダッシュボード／RSS リーダーで、ブラウザエンジンではありません。
- **WebLaunch**（2013）：Mesquite WAF として URL を全画面表示する KUAL 拡張。`settings.js` に `url` を書き、`app://com.PaulFreund.WebLaunch` で起動します。ステータスバー非表示、電源ボタンで終了などの機能があります。Kindle Touch / PW1 時代のもので、5.12 での動作は未確認ですが、「URL を設定ファイルに持たせて WAF を起動する」パターンの参考になります。([WebLaunch](https://github.com/PaulFreund/WebLaunch))
- **Illusion**：HTML/CSS/ES5 JS で WAF アプリを作るための現行ツール。`/var/local/mesquite` に配置し、`/var/local/appreg.db` に登録して `app://APP_ID` で起動します。アイコングリッドのホーム画面を作るのに向いていますが、WebKit 533 の制約を受けます。([Illusion Guide](https://github.com/penguins184/Penguins-Kindle-Wiki/blob/main/Illusion-Guide.md), [KindleModShelf](https://kindlemodshelf.me/illusion))
- **KindleForge**：Kindle 用 GUI アプリストア。WAF ベースですが、**2026-10-15 に archive** され、メンテナンスモード。最低 FW は **5.12.2.2** なので、5.12.2.1.1 ではそもそも対象外です。ホーム画面ランチャーでもありません。([KindleForge](https://github.com/KindleTweaks/KindleForge))
- **Mesquito**：Kindle Store の WAF キャッシュを乗っ取る WAF ローダー。**2026-06-20 に archive**。脱獄済みなら必須ではなく、ホーム画面ランチャーでもありません。([Mesquito](https://github.com/KindleModding/Mesquito))
- **KindleBrowserPatch**：純正ブラウザのダウンロード制限と `file://` 制限を外すパッチ。ただし対象は **FW ≥ 5.16.4 の armhf 機**で、5.12 向けではありません。([KindleBrowserPatch](https://github.com/PiperKev/KindleBrowserPatch))

### 2.3 「URL を指定して直接開く」「戻るとランチャーに戻る」はできるか

- **URL 直接起動**：5.12 では**未確認**。`app://com.lab126.browser` でブラウザを開くところまでは確実ですが、URL を渡す正式な LIPC は見つかりませんでした。実務的には「ローカル HTML リダイレクト」か「URL を埋めた WAF（WebLaunch 型）」で代替するのが堅いです。
- **戻るとランチャー**：純正ブラウザの閉じるボタンは、通常は**ネイティブの Library（`com.lab126.booklet.home`）**に戻ります。自作ランチャーに直接戻す LIPC は見つかりませんでした。
  - 代替は、`lipc-get-prop com.lab126.appmgrd activeApp` を 1 秒ごとに監視し、`com.lab126.browser` でなくなったらランチャーを再起動する「kiosk watchdog」方式です。PW6 / 5.19.6 では実証例がありますが、**5.12 での動作は未確認**です。([kindle-browser-stuff](https://github.com/onepointfive-REAL/kindle-browser-stuff), [appmgrd の activeApp](https://kindlemodding.org/kindle-apps-and-services/com.lab126.appmgrd.html))
  - ネイティブランチャーが framework を SIGSTOP している場合、ブラウザ（Mesquite WAF）を動かすには framework を SIGCONT する必要があります。ブラウザ終了後に再度 SIGSTOP してランチャーを描き直す、という状態機械が必要です。これは設計上の推論で、5.12 の実装例は未確認です。

---

## 3. ランチャー（ホーム画面）の作り方の先行例

### 3.1 OSS 例

| プロジェクト | 方式 | 状態 | 5.12 / PW4 への示唆 |
|---|---|---|---|
| **OpenReader** | FBInk + C タッチ処理、upstart で boot 置換、SIGSTOP で framework 停止、watchdog | 20★、リリースなし、最終 push 2025-10-30 | 一番近い先行例。ただしプロトタイプ。PW4 対応は未確認。([OpenReader](https://github.com/terpinedream/OpenReader)) |
| **kindlejap** | C の framebuffer ランチャー、KUAL から起動 | 1★、2026-08、基本機能のみ | シンプルな C ランチャーの参考。アプリ起動は「coming soon」段階。([kindlejap](https://github.com/victorbillyph/kindlejap)) |
| **KUAL Next** | FBInk + evdev の KUAL 互換ランチャー | 活発 | **FW ≥ 5.16.3 の hard-float 専用**。5.12 では使えない。([KUAL Next](https://github.com/thiagokokada/kual-next)) |
| **KUALA** | C++ / GTK の KUAL 書き直し | 活発 | **armhf 専用**。5.12 では使えない。([KUALA](https://github.com/kasparcode/kuala)) |
| **kindle-dash** | サーバーが生成した PNG を FBInk で表示、K4 NT で実績 | 1389★ | 「起動時に自作画面」の参考になるが、アプリ起動はしない。([kindle-dash](https://github.com/pascalw/kindle-dash)) |
| **Illusion WAF** | HTML/CSS/ES5 の WAF、appreg 登録 | 現行 | アイコングリッド型ホームに向く。ただし WebKit 533。([Illusion Guide](https://github.com/penguins184/Penguins-Kindle-Wiki/blob/main/Illusion-Guide.md)) |
| **KOReader をシェル** | File Browser / Collections / プラグイン | 現行 | アプリアイコン型ではない。「Start with」は起動画面の選択のみ。([KOReader User Guide](https://koreader.rocks/user_guide/)) |

- **5.12 では classic KUAL（Java 版）**を使います。KUALA は armhf、KUAL Next は 5.16.3+ なので対象外です。([KUAL のインストール](https://kindlemodding.org/jailbreaking/Legacy/post-jailbreak/installing-kual-mrpi/))
- **純正ホームブックレット（`com.lab126.booklet.home`）を置き換える OSS は、調べた範囲では見つかりませんでした。** 自作ホームは「KUAL から起動するアプリ」か「upstart で起動する別プロセス」として作るのが現実的です。

### 3.2 純正 UI（pillow / awesome / cvm）を止めて、KOReader／ブラウザを起動し、終了後に戻す

KOReader の `platform/kindle/koreader.sh` がそのまま実例です。([koreader.sh](https://github.com/koreader/koreader/blob/master/platform/kindle/koreader.sh))

- framework ごと止める場合（`--framework_stop`）：
  ```sh
  stop lab126_gui
  # KOReader 終了時
  start lab126_gui
  ```
  `lab126_gui` を止めると、pillow / awesome / cvm など GUI 一式が止まります。KOReader は終了時に `start lab126_gui` で戻します。
- framework を止めずに画面だけ奪う場合：
  ```sh
  lipc-set-prop com.lab126.pillow disableEnablePillow disable
  killall -STOP awesome
  # 終了時
  killall -CONT awesome
  lipc-set-prop com.lab126.pillow disableEnablePillow enable
  lipc-set-prop com.lab126.appmgrd start app://com.lab126.booklet.home
  ```
  FW 5.6.5 以降は pillow の soft hide が効かないため、KOReader は pillow を無効化し、5.7.2 以降は awesome を SIGSTOP します。5.12.2.1.1 は 5.12.4 未満なので、KOReader はタイトルバーの wmctrl 回避も行います。
- `cvm` は sysv のときだけ `killall -STOP cvm`。5.12 は upstart なので、`stop lab126_gui` で足ります。
- OpenReader はさらに `framework`、`pillow`、`blanket`、`cmd`、`phd`、`pmond`、`tmd`、`webreader`、`kfxreader` を止め、`cvm` / `lipc-wait-event` / `framework` / `pillow` を SIGSTOP します。ただし、これは**電源管理・暖房・プロセス監視まで止める**ので、そのままコピーしない方が安全です。([boot-replacement.sh](https://github.com/terpinedream/OpenReader/blob/main/openReader/bin/boot-replacement.sh))
- 純正ホームに戻すだけなら：
  ```sh
  lipc-set-prop com.lab126.appmgrd start app://com.lab126.booklet.home
  ```
  KOReader も pillow 復帰後にこれを呼んでいます。([koreader.sh](https://github.com/koreader/koreader/blob/master/platform/kindle/koreader.sh))

### 3.3 起動時に純正 Library ではなく自作 UI を出す方法

**唯一の具体例：OpenReader の upstart 置換**

- `/etc/upstart/openreader-boot.conf` を置き、`start on stopped udev-add-finished` で framework より先に起動。pre-start で `stop lab126_gui` / `stop framework` / `stop pillow` を実行し、`boot-replacement.sh` を exec します。([openreader-boot.conf](https://github.com/terpinedream/OpenReader/blob/main/openReader/openreader-boot.conf))
- `boot-replacement.sh` は：
  1. `/mnt/us/BOOT_KINDLEOS` があれば何もせず終了（ネイティブ起動）。
  2. 60 秒以内に 3 回以上起動したらフェイルセーフでネイティブ起動。
  3. framework 系を stop / SIGSTOP。
  4. watchdog を起動し、ランチャーが落ちたら再起動。
  5. `/mnt/us/extensions/openReader/bin/touch-launcher.sh` を起動。
  ([boot-replacement.sh](https://github.com/terpinedream/OpenReader/blob/main/openReader/bin/boot-replacement.sh))
- OpenReader の README には「USB から `/etc/upstart/openreader-boot.conf` を削除」とありますが、`/etc` は USB ストレージではありません。実際の非常口は **`/mnt/us/BOOT_KINDLEOS`** か SSH です。これはドキュメントの不正確さなので注意してください。([README](https://github.com/terpinedream/OpenReader))

**その他**

- `/var/local/system/` に起動フックを置く OSS は、調べた範囲では見つかりませんでした。起動制御は `/etc/upstart/` が正道です。
- KOReader の **「Start with」は起動時のホームではなく、KOReader 内の開始画面（File Browser / Favorites / History）の選択**です。端末起動時に KOReader を出す設定ではありません。([KOReader User Guide](https://koreader.rocks/user_guide/))
- WAF を「起動時のホーム」にする方法は、upstart から `lipc-set-prop ... start app://APP_ID` を叩くか、KUAL の自動起動に頼る形になります。純正ホームブックレットの置換は未文書化・高风险なので、避けた方が無難です。

---

## 4. 注意点

### 4.1 FW 5.12.2.x 固有の制約

- **soft-float（armel）**：`kindlehf` は ABI が合わず動きません。KOReader は `kindlepw2`、RakuYomi は `kindle`。([soft/hard float](https://kindlemodding.org/jailbreaking/jailbreak-faq.html))
- **ブラウザは Mesquite/WebKit 533**：現代サイトは厳しい。Chromium は 5.16.4 以降。([Mesquite 解説](https://github.com/penguins184/Penguins-Kindle-Wiki/blob/main/Mesquite-WAFs.md))
- **PW4 は FW 5.18.1.1.1 まで上がる**：上げると hard-float に移行し、`kindlepw2` / `rakuyomi-kindle` が使えなくなります。今の 5.12.2.1.1 を維持するなら OTA を止めてください。([PW4 の最新 FW](https://en.wikipedia.org/wiki/Amazon_Kindle))
- **PW4 は secure boot**：パーティションのバックアップ／リストアによる復旧ができません。KindleModding の backup ガイドも PW4 を明示的に対象外にしています。([Backing Up Your Kindle](https://kindlemodding.org/jailbreaking/Legacy/backing-up/backing-up.html))
- **KindleForge は 5.12.2.2 以上が最低要件**：5.12.2.1.1 では対象外です。しかも archive 済み。([KindleForge](https://github.com/KindleTweaks/KindleForge))

### 4.2 文鎮化リスクと回避

1. **hotfix を最新にする**：universal hotfix を Settings → Update Your Kindle で入れ、`Run Hotfix` を実行。OTA 後にも再実行が必要です。([hotfix](https://kindlemodding.org/jailbreaking/Legacy/post-jailbreak/setting-up-a-hotfix/))
2. **OTA を止める**：FW ≥ 5.11 は KUAL の `renameotabin` で `Rename`。Airplane mode で作業。([Disable OTA](https://kindlemodding.org/jailbreaking/Legacy/post-jailbreak/disable-ota.html))
3. **KUAL / SSH / kterm を残す**：boot 置換前に、SSH か kterm で `start lab126_gui` できることを確認する。
4. **boot 置換は最後**：まず WAF ホームを KUAL から起動して UX を固める。boot 置換するなら OpenReader の `BOOT_KINDLEOS`、3 回起動フェイルセーフ、watchdog、`Exit to Kindle OS` を必ず入れる。([boot-replacement.sh](https://github.com/terpinedream/OpenReader/blob/main/openReader/bin/boot-replacement.sh))
5. **framework を永久に kill しない**：SIGSTOP + watchdog で止め、必要時に SIGCONT する。`stop lab126_gui` をしたままにすると、ブラウザも Mesquite も動きません。
6. **USBMS 注意**：KOReader 実行中に USB を挿して mass storage にするとクラッシュし得ます。充電は充電器で。([KOReader wiki](https://github.com/koreader/koreader/wiki/Installation-on-Kindle-devices))
7. **core dump 抑制**：frameworkless 終了後に `KPPMainAppV2` が増えるのが嫌なら、USB ルートに空の `DISABLE_CORE_DUMP` を置く。([Kindle FAQ](https://kindlemodding.org/jailbreaking/jailbreak-faq.html))
8. **もし boot loop になったら**：
   - USB がマウントされるなら `/mnt/us/BOOT_KINDLEOS` を作って再起動。
   - SSH が生きているなら `rm /etc/upstart/openreader-boot.conf` → `reboot`、または `start lab126_gui`。
   - どちらも不可ならシリアル／DIAGS の世界ですが、**PW4 の手順は未確認**で、secure boot のため復旧保証はありません。([Backing Up](https://kindlemodding.org/jailbreaking/Legacy/backing-up/backing-up.html))

### 4.3 推奨する段階的プラン

1. **Phase 1（低リスク）**：KOReader + RakuYomi か Suwayomi plugin を入れる。KUAL から起動。ホームはまだ純正。
2. **Phase 2（中リスク）**：Illusion で WAF ホームを作る。アイコンは「新聞」「漫画」「ブラウザ」「設定」程度。KUAL から起動して UX を確認。
3. **Phase 3（高リスク）**：WAF ホームを upstart から起動、または OpenReader 型のネイティブランチャーに置換。`BOOT_KINDLEOS` とフェイルセーフを先に実装。
4. **ブラウザ**：純正ブラウザの URL 直接起動は 5.12 では未確認なので、まず「ローカル HTML リダイレクト」か「WebLaunch 型 WAF」で検証。主要サイトはサーバー側の RSS/Reader に逃がす。

---

## 参考：主要な出典

- RakuYomi 本家（archive）: https://github.com/hanatsumi/rakuyomi
- RakuYomi fork: https://github.com/tachibana-shin/rakuyomi
- RakuYomi v1.44.1: https://github.com/tachibana-shin/rakuyomi/releases/tag/v1.44.1
- RakuYomi build 表: https://github.com/tachibana-shin/rakuyomi/blob/main/docs/src/user-guide/installation/choosing-a-build.md
- RakuYomi install: https://github.com/tachibana-shin/rakuyomi/blob/main/docs/src/user-guide/installation/installing-to-your-device.md
- RakuYomi quickstart: https://github.com/tachibana-shin/rakuyomi/blob/main/docs/src/user-guide/quickstart.md
- RakuYomi settings schema: https://github.com/tachibana-shin/rakuyomi/releases/latest/download/settings.schema.json
- RakuYomi build script: https://github.com/tachibana-shin/rakuyomi/blob/main/scripts/build-all.sh
- Aidoku sources index: https://aidoku-community.github.io/sources/index.min.json
- Keiyoushi extensions: https://github.com/keiyoushi/extensions
- Keiyoushi index.pb: https://github.com/keiyoushi/extensions/raw/repo/index.pb
- KOReader v2026.07.1: https://github.com/koreader/koreader/releases/tag/v2026.07.1
- KOReader Kindle wiki: https://github.com/koreader/koreader/wiki/Installation-on-Kindle-devices
- KOReader koreader.sh: https://github.com/koreader/koreader/blob/master/platform/kindle/koreader.sh
- KOReader User Guide: https://koreader.rocks/user_guide/
- Suwayomi Server: https://github.com/Suwayomi/Suwayomi-Server
- Suwayomi v2.4.2366: https://github.com/Suwayomi/Suwayomi-Server/releases/tag/v2.4.2366
- Suwayomi KOReader plugin: https://github.com/LK4D4/suwayomi.koplugin
- Suwayomi plugin v1.4.0: https://github.com/LK4D4/suwayomi.koplugin/releases/tag/v1.4.0
- Suwayomi OPDS 実装: https://github.com/Suwayomi/Suwayomi-Server/blob/master/server/src/main/kotlin/suwayomi/tachidesk/opds/OpdsAPI.kt
- Mihon: https://github.com/mihonapp/mihon
- Kindle FAQ（soft/hard float、Android アプリ不可）: https://kindlemodding.org/jailbreaking/jailbreak-faq.html
- Kindle モデル表（PW4 の CPU/画面/FW）: https://en.wikipedia.org/wiki/Amazon_Kindle
- i.MX6 と Cortex-A9: https://en.wikipedia.org/wiki/I.MX
- Mesquite の制約: https://github.com/penguins184/Penguins-Kindle-Wiki/blob/main/Mesquite-WAFs.md
- Illusion Guide: https://github.com/penguins184/Penguins-Kindle-Wiki/blob/main/Illusion-Guide.md
- Illusion（KindleModShelf）: https://kindlemodshelf.me/illusion
- KindleForge: https://github.com/KindleTweaks/KindleForge
- Mesquito: https://github.com/KindleModding/Mesquito
- WebLaunch: https://github.com/PaulFreund/WebLaunch
- Kindle browser 実験: https://github.com/onepointfive-REAL/kindle-browser-stuff
- appmgrd: https://kindlemodding.org/kindle-apps-and-services/com.lab126.appmgrd.html
- CPR-Zero のブラウザ起動: https://cpr-zero.checkpoint.com/vulns/cprid-2181/
- Kindle bookmark editor: https://git.sr.ht/~sotirisp/kindle-bookmark-editor
- PW5 の bookmark DB: https://www.mobileread.com/forums/showthread.php?t=373851
- KindleBrowserPatch: https://github.com/PiperKev/KindleBrowserPatch
- OpenReader: https://github.com/terpinedream/OpenReader
- OpenReader boot conf: https://github.com/terpinedream/OpenReader/blob/main/openReader/openreader-boot.conf
- OpenReader boot script: https://github.com/terpinedream/OpenReader/blob/main/openReader/bin/boot-replacement.sh
- kindlejap: https://github.com/victorbillyph/kindlejap
- KUAL Next: https://github.com/thiagokokada/kual-next
- KUALA: https://github.com/kasparcode/kuala
- kindle-dash: https://github.com/pascalw/kindle-dash
- KoManga: https://github.com/mateusz464/KoManga
- PW4 がバックアップ対象外: https://kindlemodding.org/jailbreaking/Legacy/backing-up/backing-up.html
- hotfix: https://kindlemodding.org/jailbreaking/Legacy/post-jailbreak/setting-up-a-hotfix/
- OTA 無効化: https://kindlemodding.org/jailbreaking/Legacy/post-jailbreak/disable-ota.html
- リセットからの復旧: https://kindlemodding.org/jailbreaking/recovering-from-a-reset.html

VERIFIED:
- `curl -sL https://api.github.com/repos/hanatsumi/rakuyomi` → pass。`"archived": true`、`"pushed_at": "2026-01-05T04:34:26Z"` を確認。
- `curl -sL https://api.github.com/repos/tachibana-shin/rakuyomi/releases?per_page=5` → pass。`v1.44.1 2026-10-19T10:26:15Z`、assets に `rakuyomi-kindle.zip` / `rakuyomi-kindlehf.zip` / `rakuyomi-kindlea9.zip` を確認。
- `curl -sL https://api.github.com/repos/koreader/koreader/releases/latest` → pass。`v2026.07.1`、asset `koreader-kindlepw2-v2026.07.1.zip` を確認。
- `curl -sL https://api.github.com/repos/Suwayomi/Suwayomi-Server/releases/latest` → pass。`v2.4.2366`（2026-09-30）を確認。
- `curl -sL https://api.github.com/repos/LK4D4/suwayomi.koplugin/releases?per_page=5` → pass。`v1.4.0`（2026-10-07）、asset `suwayomi.koplugin-v1.4.0.zip` を確認。
- `curl -sL https://aidoku-community.github.io/sources/index.min.json` + Python 抽出 → pass。140 ソース中、`languages` に `ja` を含むのは 4 件（webアクション、コミックDAYS、ニコニコ漫画、少年ジャンプ＋）。
- `curl -sL https://raw.githubusercontent.com/keiyoushi/extensions/repo/index.json` + Python 抽出 → pass。1,420 拡張、`sources[].language == "ja"` は 179 件。
- `curl -sL https://github.com/tachibana-shin/rakuyomi/releases/latest/download/settings.schema.json` → pass。`SourceListType` に `const: "keiyoushi"`（`index.pb`）を確認。
- `git clone --depth 1 https://github.com/tachibana-shin/rakuyomi.git && grep -RIn keiyoushi` → pass。`backend/shared/Cargo.toml:116: dexvm = { version = "0.2.1", features = ["keiyoushi"] }`、`backend/shared/src/settings/schema.rs:24` を確認。
- `curl -sL https://raw.githubusercontent.com/koreader/koreader/master/platform/kindle/koreader.sh` → pass。`stop lab126_gui`、`start lab126_gui`、`lipc-set-prop com.lab126.appmgrd start app://com.lab126.booklet.home` を確認。
- `curl -sL https://raw.githubusercontent.com/terpinedream/OpenReader/main/openReader/openreader-boot.conf` → pass。`start on stopped udev-add-finished`、`exec /mnt/us/extensions/openReader/bin/boot-replacement.sh` を確認。
- `curl -sL https://raw.githubusercontent.com/terpinedream/OpenReader/main/openReader/bin/boot-replacement.sh` → pass。`BOOT_KINDLEOS`、`BOOT_COUNT > 3` のフェイルセーフを確認。
- `curl -sL https://raw.githubusercontent.com/penguins184/Penguins-Kindle-Wiki/main/Mesquite-WAFs.md` → pass。`below 5.16.4, the experimental browser uses Mesquite and not Chromium` を確認。
- `curl -sL https://raw.githubusercontent.com/penguins184/Penguins-Kindle-Wiki/main/Illusion-Guide.md` → pass。`approx. Safari 5 / WebKit 533.16`、`ES5` の記述を確認。
- `curl -sL https://git.sr.ht/~sotirisp/kindle-bookmark-editor` → pass。`/var/local/java/prefs/browser/bookmarks_wv`、Java serialized `ArrayList<BookmarkItem>` を確認。
- `curl -sL https://kindlemodding.org/jailbreaking/Legacy/backing-up/backing-up.html` → pass。PW4 が secure boot のためバックアップ対象外であることを確認。

OPEN ISSUES:
- **PW4 実機での検証はできていません。** 特に「5.12.2.1.1 で `app://com.lab126.browser#going?url=...` が通るか」「bookmarks が `bookmarks_wv` か `KPPBrowser.db` か」「RakuYomi の Keiyoushi/dexvm が PW4 で実用速度か」「suwayomi.koplugin が PW4 で快適か」は未確認です。
- `rakuyomi-kindlea9.zip` は PW4 の i.MX6 SoloLite（Cortex-A9）で理屈上は動く可能性がありますが、RakuYomi 公式表は PW4 を A9 対象に含めていません。自己責任の実験扱いです。
- WAF ホームから `appmgrd` 経由でネイティブアプリを起動する方法は、`kindle.messaging` のホワイトリスト次第で、公式手順がありません。Utild は 1★・リリースなしで、そのまま本番採用するのは危険です。([utild](https://github.com/KindleModding/utild))
- OpenReader は 20★・リリースなし・最終 push 2025-10-30 のプロトタイプです。PW4 での動作、`/dev/input/event1` のタッチ座標、バッテリー／電源管理への影響は未確認です。README の「USB から `/etc/upstart/openreader-boot.conf` を削除」は誤りで、実際は `BOOT_KINDLEOS` か SSH を使ってください。
- 純正ブラウザの TLS／暗号スイート／現代サイト互換性は、サイトごとに未確認です。5.12 の WebKit 533 では「表示できても操作できない」サイトが多いと想定してください。
- Keiyoushi には NSFW・海賊版系が含まれます。公式配信・出版社系だけを使う、各国の法令と各サイトの規約に従う、という前提が必要です。
- ローカルのコード変更はしていません。調査用に取得した一時ファイル（clone、JSON、HTML）は削除済みです。
