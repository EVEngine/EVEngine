# TARGET 1 — "Turn Based Strategy Framework" (Unity) — evidence-gathering notes

## 0. Method and evidence standard (read this first)

> **Retrieval limitation.** In the environment where this research ran, the page-fetch tool was
> non-functional (`web_fetch` failed on **every** URL tried, including `github.com`,
> `assetstore.unity.com`, `discussions.unity.com`, `www.redblobgames.com` and `en.wikipedia.org`,
> always with `URL hostname "..." resolves to a non-public IP address`), and direct HTTP from the
> shell also failed (`curl` → `schannel: AcquireCredentialsHandle failed: SEC_E_NO_CREDENTIALS`;
> `Invoke-WebRequest` → connection closed on receive). **The only working research channel was the
> `web_search` tool, which returns result *titles and URLs* but not page bodies.**
>
> **Consequence:** almost nothing below about *what the vendor actually says* is verified page
> content. What I can attest to is (a) the existence and URL of each page, (b) the page title /
> search-result fragment, and (c) short text fragments the search index happened to expose as
> result titles. Every such case is labelled inline. Items marked **[UNVERIFIED]** could not be
> checked at all; items marked **[FRAGMENT]** rest on a search-result title/snippet only.
>
> A follow-up run with working HTTP access should fetch the "what to fetch" list in §10 before
> trusting any of §2–§6.

---

## 1. Official pages, repositories and vendor documents

### 1.1 Unity Asset Store listing (official, canonical)
- **Asset page:** [Turn Based Strategy Framework | Unity Asset Store](https://assetstore.unity.com/packages/templates/systems/turn-based-strategy-framework-50282)
  — URL slug carries asset id **50282**, category path **Templates → Systems** (visible in the
  search-result title: *"Turn Based Strategy Framework | 시스템 | Unity Asset Store"*) **[FRAGMENT]**.
  Affiliate-tagged variants of the same URL appear throughout the index
  (e.g. [?aid=1011l4LKS](https://assetstore.unity.com/packages/templates/systems/turn-based-strategy-framework-50282?aid=1011l4LKS&clickref=1110l34mdP4j&utm_source=partnerize&utm_medium=affiliate&utm_campaign=unity_affiliate),
  [?source=post_page…#description](https://assetstore.unity.com/packages/templates/systems/turn-based-strategy-framework-50282?source=post_page-----f8936a475bf0---------------------------------------#description)),
  which confirms the page has a `#description` anchor section but does not reveal its text.
- **Reviews tab:** [assetstore…/50282/reviews](https://assetstore.unity.com/packages/templates/systems/turn-based-strategy-framework-50282/reviews) **[UNVERIFIED — not opened]**
- **Localised page (Korean):** [assetstore…/50282?locale=ko-KR](https://assetstore.unity.com/packages/templates/systems/turn-based-strategy-framework-50282?locale=ko-KR) **[UNVERIFIED]**

### 1.2 Documentation
- **Docs repository:** [github.com/mzetkowski/tbsf-unity-docs](https://github.com/mzetkowski/tbsf-unity-docs)
  — indexed title: *"Documentation for the Unity version of the Turn-Based Strategy Framework"*
  **[FRAGMENT]**. Note it is hosted on a **personal account (`mzetkowski`), not a `Crooked-Head`
  organisation**. I found **no GitHub organisation named "Crooked-Head"** in any search result —
  **[UNVERIFIED / negative finding]**.
- **Japanese translation of the official documentation (third-party, by "Yale"):**
  [note.com/unity_note/n/n9a005d919ff6](https://note.com/unity_note/n/n9a005d919ff6) —
  indexed fragment: *"もう一つ考慮すべき点はAIがユニットを選択する順番です"* ("another point to
  consider is the order in which the AI selects units") **[FRAGMENT]** — i.e. the official docs
  contain guidance on AI unit-selection order.
- **Legacy official documentation v1.0.1 (2016), PDF:**
  [fichier-pdf.fr — Turn Based Strategy Documentation v1.0.1](https://www.fichier-pdf.fr/2016/07/01/turn-based-strategy-documentation-v1-0-1/turn-based-strategy-documentation-v1-0-1.pdf) **[UNVERIFIED — not opened]**.
  Its existence dates the product to at least **2016**.

### 1.3 Vendor FAQ and release notes (primary vendor documents, not opened)
- **FAQ v2.0.1 (PDF, Google Drive):** [drive.google.com/file/d/1GSFdmdsN7tYSYjiU2ClqFwMJZVHiQWKW](https://drive.google.com/file/d/1GSFdmdsN7tYSYjiU2ClqFwMJZVHiQWKW/view) **[UNVERIFIED]**
- **FAQ v2.2 (PDF, Google Drive):** [drive.google.com/file/d/1k-OsykcSm5XaWIfVgO75zU2tPZpyAHLp](https://drive.google.com/file/d/1k-OsykcSm5XaWIfVgO75zU2tPZpyAHLp/view) **[UNVERIFIED]**
- **ReleaseNotes.txt (Google Drive):** [drive.google.com/file/d/1PkCrQRNQ29T7id5Fbx0--dnP55U2p94o](https://drive.google.com/file/d/1PkCrQRNQ29T7id5Fbx0--dnP55U2p94o/view) **[UNVERIFIED]**
  — titled *"ReleaseNotes.txt - ReleaseNotes"*. This is the single most valuable unread artifact for
  the changelog question.

### 1.4 Related first-party repositories (multiplayer)
- **Nakama Unity client:** [github.com/mzetkowski/tbsf-nakama-client](https://github.com/mzetkowski/tbsf-nakama-client) — *"Nakama Unity client for Turn Based Strategy Framework"* **[FRAGMENT]**
- **Nakama server (TypeScript, Dockerised):** [github.com/mzetkowski/tbsf-nakama-server](https://github.com/mzetkowski/tbsf-nakama-server) — indexed blurb: *"TypeScript-based Nakama server for Turn Based Strategy Framework, enabling online multiplayer with match management and real-time player updates. Dockerized for easy deployment."* **[FRAGMENT]**
- The official docs apparently contain a Nakama/multiplayer section: an indexed fragment of
  [tbsf-unity-docs](https://github.com/mzetkowski/tbsf-unity-docs) reads *"Nakama by Heroic Labs is
  an open-source server for realtime multiplayer games"* **[FRAGMENT]**.

### 1.5 Vendor community/support thread
- **Unity Discussions release thread:** [discussions.unity.com/t/released-turn-based-strategy-framework/749240](https://discussions.unity.com/t/released-turn-based-strategy-framework/749240)
  — the index shows post/URL positions out to at least
  [/749240/701?page=36](https://discussions.unity.com/t/released-turn-based-strategy-framework/749240/701?page=36)
  and
  [/749240/627](https://discussions.unity.com/t/released-turn-based-strategy-framework/749240/627),
  so the thread has **600+ posts across 36+ pages** — the richest single corpus of vendor statements
  and user questions in the wild **[FRAGMENT — post count/page range inferred from indexed URLs]**.

---

## 2. Advertised feature list "exactly as the vendor describes it"

**I could not obtain this.** The Asset Store `#description` section and the docs repo bodies were
not retrievable, so I will **not** paraphrase a feature list and present it as the vendor's wording.
The only vendor-documentation sentences the search index exposed are these two, quoted verbatim as
they appeared in result titles:

1. > "In the Turn-Based Strategy Framework, abilities can be assigned to units to define the actions they can perform, such as moving, …"
   — [tbsf-unity-docs](https://github.com/mzetkowski/tbsf-unity-docs) **[FRAGMENT]** (sentence truncated by the index; the ellipsis is the index's, not mine)
2. > "* **Cell Prefab** :"
   — [tbsf-unity-docs](https://github.com/mzetkowski/tbsf-unity-docs) **[FRAGMENT]** — indicates the docs describe a **Cell Prefab** configuration concept in a bulleted setup list.

Plus one code fragment, which shows the docs contain worked code against a grid controller:

3. > `var targetUnits = _gridController`
   — [tbsf-unity-docs](https://github.com/mzetkowski/tbsf-unity-docs) **[FRAGMENT]**

**Inference (clearly labelled as inference, not vendor wording):** the framework is built around the
concepts *Unit*, *Ability*, *Cell/Cell Grid*, *PlayerController vs AIController*, and a grid
controller service (`_gridController`) used to query units. The *PlayerController* / *AIController*
pairing is corroborated only indirectly, by a Ukrainian university qualification thesis that
appears to describe them in a TBSF-derived project:
[lib-repo.pnu.edu.ua — qualification thesis PDF](https://lib-repo.pnu.edu.ua/bitstream/123456789/22085/1/%d0%9a%d0%b2%d0%b0%d0%bb%d1%96%d1%84%d1%96%d0%ba%d0%b0%d1%86%d1%96%d0%b9%d0%bd%d0%b0%20%d1%80%d0%be%d0%b1%d0%be%d1%82%d0%b0_%d0%a0%d0%b8%d0%b3%d1%96%d0%bd%20%d0%ae%d1%80%d1%96%d0%b9.pdf)
(indexed fragment: *"While PlayerController relies on the human player to decide what to do,
AIController focuses…"*) **[FRAGMENT]**.

---

## 3. Version history / CHANGELOG

**No changelog text was retrievable.** What the index does show is *which versions exist in the
wild*, from third-party redistribution/mirror posts (not vendor sources — treat as weak evidence
that a version number existed, nothing more):

| Version | Where it appears | Reliability |
|---|---|---|
| v1.0.1 (2016) | [official documentation PDF, fichier-pdf.fr](https://www.fichier-pdf.fr/2016/07/01/turn-based-strategy-documentation-v1-0-1/turn-based-strategy-documentation-v1-0-1.pdf) | Medium (PDF title) |
| FAQ v2.0.1 | [Google Drive PDF](https://drive.google.com/file/d/1GSFdmdsN7tYSYjiU2ClqFwMJZVHiQWKW/view) | Medium (file title) |
| FAQ v2.2 | [Google Drive PDF](https://drive.google.com/file/d/1k-OsykcSm5XaWIfVgO75zU2tPZpyAHLp/view) | Medium (file title) |
| 3.0.5 | [cgioo.com forum post](https://www.cgioo.com/forum.php?mod=viewthread&page=1&tid=35531) (title: "【更新】Turn Based Strategy Framework 3.0.5回合制策略游戏开发框架") | Weak (mirror) |
| 4.0 | [cgioo.com](https://www.cgioo.com/forum.php?mod=viewthread&tid=38326+target=), [cgalpha.com](https://www.cgalpha.com/archives/73701.html), [cgais.com](https://www.cgais.com/archives/1284.html) | Weak (mirrors) |
| 4.0.1 | [psdly.co.uk](https://www.psdly.co.uk/turn-based-strategy-framework) | Weak (mirror) |
| 4.0.2 | [cgioo.com](https://www.cgioo.com/forum.php?mod=viewthread&tid=39273&extra=page%3D1%26filter%3Ddateline%26orderby%3Ddateline) | Weak (mirror) |

> **⚠️ Explicit negative finding on 4.2.0:** I found **no evidence anywhere in the index that version
> 4.2.0 exists**. Every 4.x reference I could surface is **4.0, 4.0.1 or 4.0.2**. The highest
> version number I could evidence is **4.0.2**. Treat "4.2.0" as **[UNVERIFIED]** — it may well
> exist (the asset is actively updated and the store page always shows the current version), but I
> could not confirm it, and I certainly could not confirm any 4.2.x changes.

The **only** authoritative changelog sources are, in priority order:
1. [ReleaseNotes.txt (Google Drive)](https://drive.google.com/file/d/1PkCrQRNQ29T7id5Fbx0--dnP55U2p94o/view) — vendor release notes file.
2. The Asset Store page's own **"Version changes" / release notes** accordion on
   [assetstore…/50282](https://assetstore.unity.com/packages/templates/systems/turn-based-strategy-framework-50282).
3. The vendor's release thread, e.g. the last pages of
   [discussions.unity.com/t/…/749240](https://discussions.unity.com/t/released-turn-based-strategy-framework/749240?page=20) — vendors usually post "vX.Y released, changes: …" there.
4. [tbsf-unity-docs](https://github.com/mzetkowski/tbsf-unity-docs) — mirrors often include a
   migration/changelog page.

---

## 4. What is new in 4.x vs 3.x

**[UNVERIFIED — could not be determined from any retrievable source.]**

What I can say about the *shape* of the question:
- The jump **3.0.5 → 4.0** is a **major** version bump, so by normal semantic-versioning convention
  it implies breaking changes and/or a re-architecture. That is an **inference from the version
  number alone**, not from any vendor statement.
- A v2.2 FAQ exists alongside a v4.x product, and the docs repo is a *separate* repository, which
  suggests the documentation was restructured at some point between 2.x and 4.x — again an
  **inference**.
- The presence of first-party **Nakama** client *and* server repositories suggests online
  multiplayer is a 4.x-era concern, but I cannot date those repositories from the index
  **[UNVERIFIED]**.

To answer this properly, diff the docs repo history and read `ReleaseNotes.txt` (§10).

---

## 5. Not supported / roadmap / "coming soon" / known limitations

**[UNVERIFIED in every particular.]** The two FAQ PDFs —
[v2.0.1](https://drive.google.com/file/d/1GSFdmdsN7tYSYjiU2ClqFwMJZVHiQWKW/view) and
[v2.2](https://drive.google.com/file/d/1k-OsykcSm5XaWIfVgO75zU2tPZpyAHLp/view) — are precisely the
artifact type that carries "known limitations / FAQ / not supported" content, and they were not
openable here. **This is the single biggest gap in Target 1.**

---

## 6. Supported Unity versions, render pipelines, 2D vs 3D

**[UNVERIFIED.]** Nothing in the retrievable index states a minimum/supported Unity version, a
render-pipeline (Built-in / URP / HDRP) position, or a 2D-vs-3D position.

Weak circumstantial signal only: one indexed snippet for the framework's docs/repo discussion
mentions **Nakama** and the framework ships a **Nakama Unity client**
([tbsf-nakama-client](https://github.com/mzetkowski/tbsf-nakama-client)), which implies a
conventional Unity project (MonoBehaviour/ScriptableObject-era APIs) rather than a pure DOTS/ECS
package. That is an **inference**, not a claim by the vendor.

---

## 7. Tutorials, walkthroughs, blog posts, forum threads, user complaints

### 7.1 Third-party tutorials and walkthroughs (existence verified; content not read)
- **Japanese tutorial series — "Turn Based Strategy Framework チュートリアル" (10 chapters):**
  [note.com/unity_note/n/n86ebdf60ef93](https://note.com/unity_note/n/n86ebdf60ef93)
  and a companion explainer
  [note.com/unity_note/n/n9b633ea606fc](https://note.com/unity_note/n/n9b633ea606fc) — titles indicate
  a *"UnityでタクティクスRPG開発"* ("build a tactics RPG in Unity") walkthrough of the framework's
  own tutorial **[FRAGMENT]**.
- **Japanese translation of the official docs:**
  [note.com/unity_note/n/n9a005d919ff6](https://note.com/unity_note/n/n9a005d919ff6) **[FRAGMENT]**.
  This is probably the best *readable* proxy for the official docs if the docs repo remains
  inaccessible.
- **Chinese deep-dive review:** [CSDN — "一站式回合制策略游戏开发框架深度推荐"](https://blog.csdn.net/2403_88403568/article/details/157093957)
  ("one-stop turn-based strategy game development framework, in-depth recommendation") **[FRAGMENT]**.
- **Chinese Unity Learn-class tutorial:** [learn.u3d.cn/tutorial/unity-turn-based](https://learn.u3d.cn/tutorial/unity-turn-based) **[UNVERIFIED]**.
- **Bilibili video:** [【Lee哥】不重复造轮子！回合制策略游戏模板 - Turn Based Strategy Framework](https://www.bilibili.com/video/av439147501/) **[FRAGMENT]**.
- **Mirror-site descriptions** (often copy the store blurb verbatim — useful for recovering the
  description text): [3DCGHUB](https://3dcghub.com/turn-based-strategy-framework/#content),
  [Codeintra](https://www.codeintra.com/items/turn-based-strategy-framework-systems-unity-assets),
  [psdly.co.uk](https://www.psdly.co.uk/turn-based-strategy-framework),
  [getasset.net](https://getasset.net/unity/3613-turn-based-strategy-framework-2.html),
  [cgais.com](https://www.cgais.com/archives/1284.html) **[UNVERIFIED]**.
- **Academic/student use of TBSF** (evidence the framework is actually used in projects):
  - Korean KCGS paper, whose indexed fragment describes the framework's win condition
    (*"…removes all units of the enemy player from the board, and if all enemy player units are
    removed, the team of the player with remaining units wins"*, in Korean):
    [kcgs.or.kr/file/download/8610](https://kcgs.or.kr/file/download/8610) **[FRAGMENT]**.
  - Ukrainian qualification thesis referencing `PlayerController` / `AIController`:
    [lib-repo.pnu.edu.ua PDF](https://lib-repo.pnu.edu.ua/bitstream/123456789/22085/1/%d0%9a%d0%b2%d0%b0%d0%bb%d1%96%d1%84%d1%96%d0%ba%d0%b0%d1%86%d1%96%d0%b9%d0%bd%d0%b0%20%d1%80%d0%be%d0%b1%d0%be%d1%82%d0%b0_%d0%a0%d0%b8%d0%b3%d1%96%d0%bd%20%d0%ae%d1%80%d1%96%d0%b9.pdf) **[FRAGMENT]**.
  - Finnish theses that appear to discuss Unity turn-based work:
    [theseus.fi/handle/10024/134060](https://www.theseus.fi/handle/10024/134060) ("Integrating AI for
    Turn-Based 4X Strategy Game") and [theseus.fi — PDF](https://www.theseus.fi/bitstream/handle/10024/855628/Mannisto_Mattias.pdf?isAllowed=y&sequence=2)
    (contains a section titled *"Näkyvyyden datatekstuurin piirtäminen"* = "drawing the visibility
    data texture", i.e. fog-of-war implementation) **[FRAGMENT — relevance to TBSF specifically is UNVERIFIED]**.

### 7.2 User complaints / pain points
**I could not verify any specific user complaint about this asset.** I am deliberately not
inventing one. Two candidate sources exist and are unread:
- the reviews tab: [assetstore…/50282/reviews](https://assetstore.unity.com/packages/templates/systems/turn-based-strategy-framework-50282/reviews);
- the 36-page community thread: [discussions.unity.com/t/…/749240](https://discussions.unity.com/t/released-turn-based-strategy-framework/749240).

One cautionary note: a search surfaced a GameDev.tv thread titled
["Unit that dies while selected still has move action"](https://community.gamedev.tv/t/unit-that-dies-while-selected-still-has-move-action/228846/3),
which *reads* like exactly the class of bug this asset would produce (stale selection state after a
unit dies mid-turn), **but nothing in the result ties that thread to TBSF** — it may belong to a
GameDev.tv course instead. **[UNVERIFIED — do not treat as a TBSF complaint.]**

---

## 8. Sample projects

- **"Clash of Heroes" — CONFIRMED to exist.**
  [apkcombo.com/clash-of-heroes-a-tbsf-demo/com.crookedhead.tbsframework.clashofheroes/](https://apkcombo.com/clash-of-heroes-a-tbsf-demo/com.crookedhead.tbsframework.clashofheroes/)
  lists an Android build titled **"Clash of Heroes - a TBSF Demo"** whose package id is
  `com.crookedhead.tbsframework.clashofheroes` **[FRAGMENT — high value]**. This is the strongest
  independent confirmation that the vendor organisation is **Crooked Head** and that a demo game
  named *Clash of Heroes* ships with/for the framework. Mirrors of the same APK listing:
  [LDPlayer (DE)](https://apk.ldplayer.net/de/games/clash-of-heroes-a-tbsf-demo-apk.html?n=35339114),
  [LDPlayer (TW)](https://www.ldplayer.tw/games/clash-of-heroes-a-tbsf-demo-on-pc.html?n=85851165).
- **"Features" tour / "Tilemap Example" / "Legacy Demos" / a tutorial scene: [ALL UNVERIFIED].**
  The only trace of any demo scene contents is the docs fragment "**Cell Prefab**"
  ([tbsf-unity-docs](https://github.com/mzetkowski/tbsf-unity-docs)) which is consistent with a
  sample scene built from cell prefabs, but that is an **inference**.

---

## 9. Pricing, licensing, source availability

**[UNVERIFIED.]** No retrievable result states a price, the licence terms, or whether full C# source
ships. General knowledge (flagged as **not verified in this session**): Unity Asset Store template
assets are sold under the
[Unity Asset Store EULA](https://unity.com/legal/as-terms) and typically ship as full source because
Unity cannot ship compiled DLL-only assets usefully — but for *this* asset I did not confirm it.

---

## 10. "What to fetch" list for a follow-up run with working HTTP

In priority order:
1. `https://drive.google.com/file/d/1PkCrQRNQ29T7id5Fbx0--dnP55U2p94o/view` — **ReleaseNotes.txt** (changelog!)
2. `https://drive.google.com/file/d/1k-OsykcSm5XaWIfVgO75zU2tPZpyAHLp/view` — **FAQ v2.2** (limitations/roadmap)
3. `https://drive.google.com/file/d/1GSFdmdsN7tYSYjiU2ClqFwMJZVHiQWKW/view` — **FAQ v2.0.1**
4. `https://assetstore.unity.com/packages/templates/systems/turn-based-strategy-framework-50282` — description + "Version changes" accordion (current version, price, Unity version, source included)
5. `https://github.com/mzetkowski/tbsf-unity-docs` — clone; `git log` gives the doc-side changelog
6. `https://discussions.unity.com/t/released-turn-based-strategy-framework/749240?page=1..36` — vendor release posts + complaints
7. `https://github.com/mzetkowski/tbsf-nakama-client` and `.../tbsf-nakama-server` — READMEs (multiplayer scope, Unity version)
8. `https://assetstore.unity.com/packages/templates/systems/turn-based-strategy-framework-50282/reviews` — user complaints
9. `https://note.com/unity_note/n/n9a005d919ff6` — JP translation of official docs (readable feature list)
10. `https://blog.csdn.net/2403_88403568/article/details/157093957` — CN deep-dive feature write-up
