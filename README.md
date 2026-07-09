# Wrap-It: Universal Web-to-Native Engine

Wrap-It is a hyper-optimized, custom-built C++ browser engine wrapper that allows you to turn **any website** into a standalone, native Windows desktop application. 

Instead of downloading heavy Electron apps for Discord, WhatsApp, Spotify, or Notion, you can use one single 2MB engine to wrap them all. Because it uses the native Windows WebView2 runtime and direct Win32 memory management, it uses a fraction of the RAM of standard desktop apps.

## 🚀 Core Features
* **Universal Wrapping:** Turn *any* URL into an app just by changing a text file.
* **Aggressive Memory Trimming:** Intercepts the Windows "Close" button to send the app to the System Tray. When hidden, it uses the Windows PSAPI (`EmptyWorkingSet`) to forcibly flush unused RAM, dropping background memory to ~5MB-10MB.
* **Native C++ Dark Mode:** Capable of injecting custom CSS to hardware-invert website colors *before* the DOM renders, forcing a dark mode on sites that don't natively support it.
* **Isolated Caching:** Run multiple instances (e.g., multiple Discord accounts) simultaneously. Each app folder creates its own isolated browser cache, preventing login conflicts.
* **High-DPI Per-Monitor V2:** Graphics and text remain razor-sharp across multiple monitors with different scaling setups.

---

## 🛠️ Installation & Setup

Because Wrap-It is a portable application, there is no installer. 

1. Download the latest `Wrap-It.zip` from the **Releases** tab.
2. Extract the folder anywhere on your PC (e.g., to your Desktop).
3. Inside the folder, you will see three files: `wrap it.exe`, `WebView2Loader.dll`, and `config.txt`.

### ⚙️ How to configure your app (The `config.txt` file)
To change what website the app loads, you must open the `config.txt` file in Notepad and edit it. 

**The file MUST have exactly 3 lines of text.** Do not add extra spaces, and do not leave blank lines at the top.

* **Line 1: The App Title** (This will appear on the taskbar and system tray hover).
* **Line 2: The Target URL** (Must include `https://`).
* **Line 3: Dark Mode Toggle** (Type `true` to force dark mode, or `false` to leave the website normal).
* **Line 4: Aggressive Memory Saver (Type `true` to save memory aggressively. Recommended to keep disabled as the impact is minimal).
* **Line 5: Quit On Close (Type `true` to close the app without minimizing to system tray).

**Example `config.txt` for WhatsApp:**
```text
name = WhatsApp Lite
link = https://web.whatsapp.com
darkmode = true
aggressivememory = false
quitonclose = true
```

**Example `config.txt` for Discord:**
```text
name = Discord
link = https://discord.com/app
darkmode = false
aggressivememory = false
quitonclose = true
```

Once you save the `config.txt`, simply double-click the `.exe` and enjoy your new lightweight app! You can rename the `.exe` file to whatever you want.

---
## 💻 Built With
* C++ & Win32 API
* Microsoft Edge WebView2 Runtime


## Credits

*   **App Icon:** The app logo is a modified version of a chameleon design by Aslan from [SVGRepo](https://www.svgrepo.com/svg/500089/chameleon). The original artwork was altered to include the "WRAP IT" text and adjust the layout. Used under the [Creative Commons Attribution License](https://creativecommons.org/licenses/by/4.0/).
