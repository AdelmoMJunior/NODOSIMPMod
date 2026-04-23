// nodosimp.cpp
// Impressao termica direta via GDI (substitui copia para pasta PRINT/winprint)
// Configuracoes por usuario/maquina em %APPDATA%\NODOSIMP\nodosimp.ini
// Compilar com: g++ -std=c++17 -o nodosimp.exe nodosimp.cpp
//               -lcomdlg32 -lgdi32 -lwinspool -luser32 -lshell32
// Ou no MSVC: adicionar os .lib abaixo nos pragmas

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <filesystem>
#include <thread>
#include <algorithm>
#include <sstream>
#include <cstdio>
#include <windows.h>
#include <tlhelp32.h>
#include <winspool.h>
#include <commdlg.h>
#include <wingdi.h>
#include <shellapi.h>

#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "winspool.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")

namespace fs = std::filesystem;

// ============================================================
//  FUNCOES ORIGINAIS (inalteradas)
// ============================================================

bool IsProcessRunning(const std::string& processName) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32W processEntry;
    processEntry.dwSize = sizeof(PROCESSENTRY32W);

    if (Process32FirstW(snapshot, &processEntry)) {
        std::wstring targetProcess(processName.begin(), processName.end());
        do {
            std::wstring currentProcess(processEntry.szExeFile);
            if (_wcsicmp(currentProcess.c_str(), targetProcess.c_str()) == 0) {
                CloseHandle(snapshot);
                return true;
            }
        } while (Process32NextW(snapshot, &processEntry));
    }

    CloseHandle(snapshot);
    return false;
}

bool PastaEstaVazia(const std::string& pastaPath) {
    return fs::is_empty(pastaPath);
}

void RetiraULinha(const std::string& cam) {
    std::vector<std::string> linhas;
    std::string linha;
    std::ifstream arquivo(cam);

    while (std::getline(arquivo, linha)) {
        linhas.push_back(linha);
    }
    arquivo.close();

    if (!linhas.empty()) {
        // So remove a ultima linha se ela NAO contiver letra, numero ou '-'
        const std::string& ultimaLinha = linhas.back();
        bool temConteudo = false;
        for (char c : ultimaLinha) {
            if (std::isalnum((unsigned char)c) || c == '-') {
                temConteudo = true;
                break;
            }
        }
        if (!temConteudo) {
            linhas.pop_back();
        }

        int lines = 0;

        for (size_t i = 0; i < linhas.size(); i++) {
            lines++;
            if (i == 0) {
                linhas[i] = "";
            } else if (linhas[i].length() > 140) {
                linhas[i] = linhas[i].substr(0, 140);
            }
        }

        std::ofstream outFile(cam);
        for (const auto& l : linhas) {
            outFile << l << std::endl;
        }
        outFile.close();

        if (lines < 6) {
            fs::remove(cam);
        }
    }
}

// ============================================================
//  CONFIGURACAO
// ============================================================

struct PrintConfig {
    std::string printerName;
    char        fontName[LF_FACESIZE];  // ex: "Courier New"
    int         fontSize;               // pontos
    int         fontWeight;             // FW_NORMAL / FW_BOLD
    BOOL        fontItalic;
    int         fontCharset;
    double      marginLeftMm;           // margem esquerda em mm
    double      marginTopMm;            // margem topo em mm
    std::string logoPath;               // caminho do BMP da logo (opcional)
    double      logoLeftMm;             // posicao horizontal da logo em mm
    double      logoTopMm;              // posicao vertical   da logo em mm
    bool        isConfigured;

    PrintConfig() {
        printerName  = "";
        strncpy_s(fontName, "Courier New", LF_FACESIZE - 1);
        fontSize     = 9;
        fontWeight   = FW_NORMAL;
        fontItalic   = FALSE;
        fontCharset  = DEFAULT_CHARSET;
        marginLeftMm = 5.0;
        marginTopMm  = 5.0;
        logoPath     = "";
        logoLeftMm   = 0.0;
        logoTopMm    = 0.0;
        isConfigured = false;
    }
};

// ---------- Caminho do INI: %APPDATA%\NODOSIMP\nodosimp.ini ----------

static std::string GetConfigPath() {
    char appData[MAX_PATH] = "";
    if (GetEnvironmentVariableA("APPDATA", appData, MAX_PATH) == 0) {
        GetTempPathA(MAX_PATH, appData);
    }
    std::string dir = std::string(appData) + "\\NODOSIMP";
    CreateDirectoryA(dir.c_str(), NULL);   // cria se nao existir
    return dir + "\\nodosimp.ini";
}

static PrintConfig LoadConfig() {
    PrintConfig cfg;
    std::string path = GetConfigPath();
    const char* S    = "Print";
    char buf[512]    = "";

    GetPrivateProfileStringA(S, "Printer", "", buf, sizeof(buf), path.c_str());
    cfg.printerName = buf;

    GetPrivateProfileStringA(S, "Font", "Courier New", buf, sizeof(buf), path.c_str());
    strncpy_s(cfg.fontName, buf, LF_FACESIZE - 1);

    cfg.fontSize    = GetPrivateProfileIntA(S, "FontSize",    9,              path.c_str());
    cfg.fontWeight  = GetPrivateProfileIntA(S, "FontWeight",  FW_NORMAL,      path.c_str());
    cfg.fontItalic  = GetPrivateProfileIntA(S, "FontItalic",  0,              path.c_str());
    cfg.fontCharset = GetPrivateProfileIntA(S, "FontCharset", DEFAULT_CHARSET, path.c_str());

    // margens salvas como centesimos de mm (evita ponto flutuante no INI)
    cfg.marginLeftMm = GetPrivateProfileIntA(S, "MarginLeft100",  500, path.c_str()) / 100.0;
    cfg.marginTopMm  = GetPrivateProfileIntA(S, "MarginTop100",   500, path.c_str()) / 100.0;

    GetPrivateProfileStringA(S, "LogoPath", "", buf, sizeof(buf), path.c_str());
    cfg.logoPath    = buf;

    cfg.logoLeftMm  = GetPrivateProfileIntA(S, "LogoLeft100", 0, path.c_str()) / 100.0;
    cfg.logoTopMm   = GetPrivateProfileIntA(S, "LogoTop100",  0, path.c_str()) / 100.0;

    cfg.isConfigured = !cfg.printerName.empty();
    return cfg;
}

static void SaveConfig(const PrintConfig& cfg) {
    std::string path = GetConfigPath();
    const char* S    = "Print";
    char buf[64]     = "";

    WritePrivateProfileStringA(S, "Printer",       cfg.printerName.c_str(), path.c_str());
    WritePrivateProfileStringA(S, "Font",           cfg.fontName,            path.c_str());

    sprintf_s(buf, "%d", cfg.fontSize);
    WritePrivateProfileStringA(S, "FontSize",       buf, path.c_str());

    sprintf_s(buf, "%d", cfg.fontWeight);
    WritePrivateProfileStringA(S, "FontWeight",     buf, path.c_str());

    sprintf_s(buf, "%d", (int)cfg.fontItalic);
    WritePrivateProfileStringA(S, "FontItalic",     buf, path.c_str());

    sprintf_s(buf, "%d", cfg.fontCharset);
    WritePrivateProfileStringA(S, "FontCharset",    buf, path.c_str());

    sprintf_s(buf, "%d", (int)(cfg.marginLeftMm * 100));
    WritePrivateProfileStringA(S, "MarginLeft100",  buf, path.c_str());

    sprintf_s(buf, "%d", (int)(cfg.marginTopMm * 100));
    WritePrivateProfileStringA(S, "MarginTop100",   buf, path.c_str());

    WritePrivateProfileStringA(S, "LogoPath",       cfg.logoPath.c_str(), path.c_str());

    sprintf_s(buf, "%d", (int)(cfg.logoLeftMm * 100));
    WritePrivateProfileStringA(S, "LogoLeft100",    buf, path.c_str());

    sprintf_s(buf, "%d", (int)(cfg.logoTopMm * 100));
    WritePrivateProfileStringA(S, "LogoTop100",     buf, path.c_str());
}

// ============================================================
//  UTILITARIOS DE IMPRESSORA
// ============================================================

static std::vector<std::string> EnumInstalledPrinters() {
    std::vector<std::string> list;
    DWORD needed = 0, count = 0;
    EnumPrintersA(PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS,
                  NULL, 2, NULL, 0, &needed, &count);
    if (needed == 0) return list;
    std::vector<BYTE> buf(needed);
    if (EnumPrintersA(PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS,
                      NULL, 2, buf.data(), needed, &needed, &count)) {
        PRINTER_INFO_2A* info = reinterpret_cast<PRINTER_INFO_2A*>(buf.data());
        for (DWORD i = 0; i < count; i++) {
            if (info[i].pPrinterName)
                list.push_back(info[i].pPrinterName);
        }
    }
    return list;
}

static bool PrinterExists(const std::string& name) {
    if (name.empty()) return false;
    HANDLE h;
    if (OpenPrinterA(const_cast<LPSTR>(name.c_str()), &h, NULL)) {
        ClosePrinter(h);
        return true;
    }
    return false;
}

// ============================================================
//  DIALOGO DE CONFIGURACAO
// ============================================================

// IDs dos controles
#define IDC_CMB_PRINTER   201
#define IDC_EDT_FONT      202
#define IDC_BTN_FONT      203
#define IDC_EDT_MLEFT     204
#define IDC_EDT_MTOP      205
#define IDC_EDT_LOGO      206
#define IDC_BTN_LOGO      207
#define IDC_EDT_LLEFT     208
#define IDC_EDT_LTOP      209
#define IDC_BTN_SAVE      210
#define IDC_BTN_CANCEL    211

struct ConfigDlgData {
    PrintConfig cfg;
    LOGFONTA    lf;
    bool        saved;
};

// Atualiza o campo de exibicao da fonte
static void UpdateFontDisplay(HWND hwnd, const PrintConfig& cfg) {
    char buf[256];
    sprintf_s(buf, "%s, %dpt%s%s",
              cfg.fontName,
              cfg.fontSize,
              cfg.fontWeight >= FW_BOLD ? ", Negrito" : "",
              cfg.fontItalic            ? ", Italico" : "");
    SetDlgItemTextA(hwnd, IDC_EDT_FONT, buf);
}

// Cria um controle filho com label a esquerda
static int CreateLabeledRow(HWND parent, const char* label,
                             int ctrlId, const char* initText,
                             int ctrlStyle, int x, int y,
                             int labelW, int editW, int h) {
    CreateWindowA("STATIC", label,
        WS_CHILD | WS_VISIBLE | SS_RIGHT,
        x, y + 3, labelW, h, parent, NULL, NULL, NULL);
    CreateWindowA("EDIT", initText,
        WS_CHILD | WS_VISIBLE | WS_BORDER | ctrlStyle,
        x + labelW + 6, y, editW, h, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ctrlId)), NULL, NULL);
    return y + h + 7;
}

static LRESULT CALLBACK ConfigWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    ConfigDlgData* d =
        reinterpret_cast<ConfigDlgData*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));

    switch (msg) {

    // ---- Criacao da janela -----------------------------------------------
    case WM_CREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTA*>(lParam);
        d = reinterpret_cast<ConfigDlgData*>(cs->lpCreateParams);
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(d));

        const int LW = 155;  // largura do label
        const int EW = 240;  // largura do campo
        const int H  = 22;   // altura linha
        const int X  = 10;   // margem esquerda
        int       y  = 12;

        // ---- Impressora (combobox) ----
        CreateWindowA("STATIC", "Impressora:",
            WS_CHILD | WS_VISIBLE | SS_RIGHT,
            X, y + 3, LW, H, hwnd, NULL, NULL, NULL);

        HWND hCmb = CreateWindowA("COMBOBOX", "",
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
            X + LW + 6, y, EW, 220, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CMB_PRINTER)),
            NULL, NULL);
        {
            auto printers = EnumInstalledPrinters();
            int sel = 0;
            for (int i = 0; i < (int)printers.size(); i++) {
                SendMessageA(hCmb, CB_ADDSTRING, 0,
                             reinterpret_cast<LPARAM>(printers[i].c_str()));
                if (printers[i] == d->cfg.printerName) sel = i;
            }
            if (!printers.empty())
                SendMessageA(hCmb, CB_SETCURSEL, sel, 0);
        }
        y += H + 7;

        // ---- Fonte ----
        CreateWindowA("STATIC", "Fonte:",
            WS_CHILD | WS_VISIBLE | SS_RIGHT,
            X, y + 3, LW, H, hwnd, NULL, NULL, NULL);
        CreateWindowA("EDIT", "",
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_READONLY,
            X + LW + 6, y, EW - 82, H, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_EDT_FONT)), NULL, NULL);
        CreateWindowA("BUTTON", "Selecionar...",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            X + LW + 6 + EW - 78, y, 78, H, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_FONT)), NULL, NULL);
        UpdateFontDisplay(hwnd, d->cfg);
        y += H + 7;

        // ---- Margem esquerda ----
        {
            char buf[32]; sprintf_s(buf, "%.1f", d->cfg.marginLeftMm);
            y = CreateLabeledRow(hwnd, "Margem Esquerda (mm):",
                                 IDC_EDT_MLEFT, buf, 0, X, y, LW, 80, H);
        }
        // ---- Margem topo ----
        {
            char buf[32]; sprintf_s(buf, "%.1f", d->cfg.marginTopMm);
            y = CreateLabeledRow(hwnd, "Margem Superior (mm):",
                                 IDC_EDT_MTOP, buf, 0, X, y, LW, 80, H);
        }

        // ---- Logo (caminho) ----
        CreateWindowA("STATIC", "Logo (arquivo BMP):",
            WS_CHILD | WS_VISIBLE | SS_RIGHT,
            X, y + 3, LW, H, hwnd, NULL, NULL, NULL);
        CreateWindowA("EDIT", d->cfg.logoPath.c_str(),
            WS_CHILD | WS_VISIBLE | WS_BORDER,
            X + LW + 6, y, EW - 82, H, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_EDT_LOGO)), NULL, NULL);
        CreateWindowA("BUTTON", "Procurar...",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            X + LW + 6 + EW - 78, y, 78, H, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_LOGO)), NULL, NULL);
        y += H + 7;

        // ---- Logo margem esquerda ----
        {
            char buf[32]; sprintf_s(buf, "%.1f", d->cfg.logoLeftMm);
            y = CreateLabeledRow(hwnd, "Logo - Margem Esq. (mm):",
                                 IDC_EDT_LLEFT, buf, 0, X, y, LW, 80, H);
        }
        // ---- Logo margem topo ----
        {
            char buf[32]; sprintf_s(buf, "%.1f", d->cfg.logoTopMm);
            y = CreateLabeledRow(hwnd, "Logo - Margem Sup. (mm):",
                                 IDC_EDT_LTOP, buf, 0, X, y, LW, 80, H);
        }

        y += 6;
        // ---- Botoes ----
        CreateWindowA("BUTTON", "Salvar",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP,
            X + LW + 6, y, 100, 28, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_SAVE)), NULL, NULL);
        CreateWindowA("BUTTON", "Cancelar",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            X + LW + 6 + 108, y, 100, 28, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_CANCEL)), NULL, NULL);

        return 0;
    }

    // ---- Comandos dos botoes ------------------------------------------------
    case WM_COMMAND: {
        if (!d) return 0;
        int id = LOWORD(wParam);

        // Selecionar fonte
        if (id == IDC_BTN_FONT) {
            CHOOSEFONTA cf = {};
            cf.lStructSize = sizeof(cf);
            cf.hwndOwner   = hwnd;
            cf.lpLogFont   = &d->lf;
            cf.Flags       = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT | CF_NOSCRIPTSEL;

            // Inicializa LOGFONT com config atual
            ZeroMemory(&d->lf, sizeof(d->lf));
            strncpy_s(d->lf.lfFaceName, d->cfg.fontName, LF_FACESIZE - 1);
            {
                HDC hdc = GetDC(hwnd);
                d->lf.lfHeight = -MulDiv(d->cfg.fontSize,
                                          GetDeviceCaps(hdc, LOGPIXELSY), 72);
                ReleaseDC(hwnd, hdc);
            }
            d->lf.lfWeight  = d->cfg.fontWeight;
            d->lf.lfItalic  = (BYTE)d->cfg.fontItalic;
            d->lf.lfCharSet = (BYTE)d->cfg.fontCharset;

            if (ChooseFontA(&cf)) {
                strncpy_s(d->cfg.fontName, d->lf.lfFaceName, LF_FACESIZE - 1);
                // cf.iPointSize esta em decimos de ponto
                d->cfg.fontSize   = cf.iPointSize / 10;
                if (d->cfg.fontSize <= 0) d->cfg.fontSize = 9;
                d->cfg.fontWeight  = d->lf.lfWeight;
                d->cfg.fontItalic  = d->lf.lfItalic;
                d->cfg.fontCharset = d->lf.lfCharSet;
                UpdateFontDisplay(hwnd, d->cfg);
            }
        }

        // Procurar logo BMP
        else if (id == IDC_BTN_LOGO) {
            char filename[MAX_PATH] = "";
            // Pre-preenche com o valor atual
            GetDlgItemTextA(hwnd, IDC_EDT_LOGO, filename, MAX_PATH);

            OPENFILENAMEA ofn = {};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner   = hwnd;
            ofn.lpstrFilter = "Imagens BMP\0*.bmp\0Todos os arquivos\0*.*\0";
            ofn.lpstrFile   = filename;
            ofn.nMaxFile    = MAX_PATH;
            ofn.lpstrTitle  = "Selecionar Logo BMP";
            ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

            if (GetOpenFileNameA(&ofn))
                SetDlgItemTextA(hwnd, IDC_EDT_LOGO, filename);
        }

        // Salvar
        else if (id == IDC_BTN_SAVE) {
            int idx = (int)SendDlgItemMessageA(hwnd, IDC_CMB_PRINTER,
                                               CB_GETCURSEL, 0, 0);
            if (idx == CB_ERR) {
                MessageBoxA(hwnd, "Selecione uma impressora.",
                            "Aviso", MB_OK | MB_ICONWARNING);
                return 0;
            }
            char pName[256] = "";
            SendDlgItemMessageA(hwnd, IDC_CMB_PRINTER,
                                CB_GETLBTEXT, idx, reinterpret_cast<LPARAM>(pName));
            d->cfg.printerName = pName;

            char buf[MAX_PATH] = "";

            GetDlgItemTextA(hwnd, IDC_EDT_MLEFT, buf, sizeof(buf));
            d->cfg.marginLeftMm = atof(buf);

            GetDlgItemTextA(hwnd, IDC_EDT_MTOP, buf, sizeof(buf));
            d->cfg.marginTopMm  = atof(buf);

            GetDlgItemTextA(hwnd, IDC_EDT_LOGO, buf, sizeof(buf));
            d->cfg.logoPath = buf;

            GetDlgItemTextA(hwnd, IDC_EDT_LLEFT, buf, sizeof(buf));
            d->cfg.logoLeftMm = atof(buf);

            GetDlgItemTextA(hwnd, IDC_EDT_LTOP, buf, sizeof(buf));
            d->cfg.logoTopMm  = atof(buf);

            SaveConfig(d->cfg);
            d->saved = true;
            DestroyWindow(hwnd);
        }

        // Cancelar
        else if (id == IDC_BTN_CANCEL) {
            DestroyWindow(hwnd);
        }

        return 0;
    }

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

// Exibe o dialogo de configuracao (bloqueante).
// Retorna true se o usuario salvou.
static bool ShowConfigDialog(PrintConfig& cfg,
                              const char* titulo = "Configuracao de Impressao - NODOSIMP") {
    const char* WNDCLASS_NAME = "NodosimpConfigDlg";

    // Registra a classe apenas uma vez por processo
    WNDCLASSA wc = {};
    wc.lpfnWndProc   = ConfigWndProc;
    wc.hInstance     = GetModuleHandleA(NULL);
    wc.lpszClassName = WNDCLASS_NAME;
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&wc);   // ignora erro se ja registrado

    ConfigDlgData data;
    data.cfg   = cfg;
    data.saved = false;
    ZeroMemory(&data.lf, sizeof(data.lf));

    // Tamanho da janela: largura = X(10) + LW(155) + 6 + EW(240) + 20(borda) = ~440
    //                   altura  = 8 linhas * 29 + botoes(34) + titulo(~30) + padding = ~330
    HWND hwnd = CreateWindowExA(
        WS_EX_DLGMODALFRAME,
        WNDCLASS_NAME, titulo,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 448, 348,
        NULL, NULL, GetModuleHandleA(NULL), &data);

    if (!hwnd) return false;

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    // Loop de mensagens local (bloqueia ate a janela fechar)
    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        if (!IsDialogMessageA(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }

    if (data.saved) {
        cfg = data.cfg;
        return true;
    }
    return false;
}

// ============================================================
//  IMPRESSAO GDI
// ============================================================

// Converte milimetros para unidades do dispositivo (pixels da impressora)
static int MmToDev(HDC hdc, double mm, bool horizontal) {
    int physPx = horizontal ? GetDeviceCaps(hdc, HORZRES)  : GetDeviceCaps(hdc, VERTRES);
    int physMm = horizontal ? GetDeviceCaps(hdc, HORZSIZE) : GetDeviceCaps(hdc, VERTSIZE);
    if (physMm <= 0) return 0;
    return static_cast<int>(mm * physPx / physMm);
}

// Imprime o arquivo texto usando as configuracoes salvas.
// Retorna true em caso de sucesso.
static bool PrintTextFile(const std::string& filePath, const PrintConfig& cfg) {
    // ---- Abre o arquivo ----
    std::ifstream file(filePath);
    if (!file.is_open()) return false;

    std::vector<std::string> linhas;
    std::string linha;
    while (std::getline(file, linha)) {
        // Remove \r residual (arquivos DOS/Windows)
        if (!linha.empty() && linha.back() == '\r')
            linha.pop_back();
        linhas.push_back(linha);
    }
    file.close();

    if (linhas.empty()) return true;  // nada a imprimir

    // ---- Cria DC da impressora ----
    HDC hdc = CreateDCA(NULL, cfg.printerName.c_str(), NULL, NULL);
    if (!hdc) return false;

    // ---- Cria a fonte ----
    LOGFONTA lf = {};
    strncpy_s(lf.lfFaceName, cfg.fontName, LF_FACESIZE - 1);
    lf.lfHeight       = -MulDiv(cfg.fontSize, GetDeviceCaps(hdc, LOGPIXELSY), 72);
    lf.lfWeight       = cfg.fontWeight;
    lf.lfItalic       = (BYTE)cfg.fontItalic;
    lf.lfCharSet      = (BYTE)cfg.fontCharset;
    lf.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;

    HFONT hFont    = CreateFontIndirectA(&lf);
    HFONT hOldFont = reinterpret_cast<HFONT>(SelectObject(hdc, hFont));

    // ---- Metricas de texto ----
    TEXTMETRICA tm = {};
    GetTextMetricsA(hdc, &tm);
    int lineHeight = tm.tmHeight + tm.tmExternalLeading;

    // ---- Dimensoes da pagina ----
    int pageW = GetDeviceCaps(hdc, HORZRES);
    int pageH = GetDeviceCaps(hdc, VERTRES);

    // ---- Margens em pixels ----
    int marginLeft   = MmToDev(hdc, cfg.marginLeftMm, true);
    int marginTop    = MmToDev(hdc, cfg.marginTopMm,  false);
    int marginBottom = MmToDev(hdc, 5.0,              false);  // 5 mm fixo inferior
    int printableBottom = pageH - marginBottom;

    // ---- Carrega logo BMP (opcional) ----
    HBITMAP hLogo = NULL;
    int logoW = 0, logoH = 0;
    int logoX = MmToDev(hdc, cfg.logoLeftMm, true);
    int logoY = MmToDev(hdc, cfg.logoTopMm,  false);

    if (!cfg.logoPath.empty() && fs::exists(cfg.logoPath)) {
        hLogo = reinterpret_cast<HBITMAP>(
            LoadImageA(NULL, cfg.logoPath.c_str(), IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE));
        if (hLogo) {
            BITMAP bmp = {};
            GetObjectA(hLogo, sizeof(bmp), &bmp);
            logoW = bmp.bmWidth;
            logoH = bmp.bmHeight;
        }
    }

    // ---- Inicia documento ----
    DOCINFOA di   = {};
    di.cbSize     = sizeof(di);
    di.lpszDocName = filePath.c_str();

    if (StartDocA(hdc, &di) <= 0) {
        SelectObject(hdc, hOldFont);
        DeleteObject(hFont);
        if (hLogo) DeleteObject(hLogo);
        DeleteDC(hdc);
        return false;
    }

    // ---- Imprime pagina a pagina ----
    int lineIdx   = 0;
    bool firstPage = true;

    while (lineIdx < (int)linhas.size()) {
        StartPage(hdc);

        int y = marginTop;

        // Desenha logo apenas na primeira pagina
        if (firstPage && hLogo) {
            HDC hdcMem = CreateCompatibleDC(hdc);
            HBITMAP hOldBmp = reinterpret_cast<HBITMAP>(SelectObject(hdcMem, hLogo));

            // Escala o BMP para caber na largura disponivel se necessario
            int drawW = std::min(logoW, pageW - logoX);
            int drawH = (drawW < logoW) ? (int)((double)logoH * drawW / logoW) : logoH;

            StretchBlt(hdc, logoX, logoY, drawW, drawH,
                       hdcMem, 0, 0, logoW, logoH, SRCCOPY);

            SelectObject(hdcMem, hOldBmp);
            DeleteDC(hdcMem);

            y = logoY + drawH + lineHeight;  // texto comeca abaixo da logo
        }
        firstPage = false;

        // Imprime linhas ate o final da pagina
        int startLineIdx = lineIdx;
        while (lineIdx < (int)linhas.size()) {
            if (y + lineHeight > printableBottom) break;  // quebra de pagina

            const std::string& ln = linhas[lineIdx];
            if (!ln.empty())
                TextOutA(hdc, marginLeft, y, ln.c_str(), (int)ln.size());

            y += lineHeight;
            lineIdx++;
        }

        // Protecao contra loop infinito (linha maior que a pagina inteira)
        if (lineIdx == startLineIdx) lineIdx++;

        EndPage(hdc);
    }

    EndDoc(hdc);

    // ---- Limpeza ----
    SelectObject(hdc, hOldFont);
    DeleteObject(hFont);
    if (hLogo) DeleteObject(hLogo);
    DeleteDC(hdc);

    return true;
}

// ============================================================
//  imptermica - NOVO COMPORTAMENTO (impressao GDI direta)
// ============================================================

void imptermica(const std::string& cam) {
    PrintConfig cfg = LoadConfig();

    // Sem configuracao: abre dialogo antes de imprimir
    if (!cfg.isConfigured) {
        MessageBoxA(NULL,
            "Impressora nao configurada.\n"
            "A tela de configuracao sera aberta.\n\n"
            "Selecione a impressora, fonte e margens desejadas.",
            "NODOSIMP - Configuracao necessaria",
            MB_OK | MB_ICONINFORMATION);

        if (!ShowConfigDialog(cfg, "Configuracao de Impressao - NODOSIMP")) {
            std::cout << "Impressao cancelada: configuracao nao salva." << std::endl;
            return;
        }
    }

    // Tenta imprimir
    bool ok = false;

    if (PrinterExists(cfg.printerName)) {
        ok = PrintTextFile(cam, cfg);
    }

    if (!ok) {
        // Impressora sumiu ou erro: pede para selecionar outra
        std::string msg =
            "Nao foi possivel imprimir na impressora:\n\"" +
            cfg.printerName + "\"\n\n"
            "A tela de configuracao sera aberta para selecionar outra impressora.";
        MessageBoxA(NULL, msg.c_str(),
                    "NODOSIMP - Erro de Impressao", MB_OK | MB_ICONWARNING);

        if (ShowConfigDialog(cfg, "Selecionar Impressora - NODOSIMP")) {
            ok = PrintTextFile(cam, cfg);
        }

        if (!ok) {
            MessageBoxA(NULL,
                "Falha ao imprimir. Verifique se a impressora esta ligada e disponivel.",
                "NODOSIMP - Erro", MB_OK | MB_ICONERROR);
        }
    }
}

// ============================================================
//  main  (logica original preservada; argc < 2 abre config)
// ============================================================

int main(int argc, char* argv[]) {

    // Sem argumentos: abre tela de configuracao
    if (argc < 2) {
        PrintConfig cfg = LoadConfig();
        ShowConfigDialog(cfg, "Configuracao de Impressao - NODOSIMP");
        return 0;
    }

    int termica = 0;
    try {
        std::ifstream arquivo(argv[1]);
        std::vector<std::string> linhas;
        std::string linha;

        while (std::getline(arquivo, linha)) {

            if (linha.find("    CodigoEAN    Quant Un   ValorUnit  TotalItem") != std::string::npos ||
                linha.find("Marca            Quant Un   ValorUnit  TotalItem") != std::string::npos ||
                linha.find("Referencia       QNTD/PED    QNTD/OE  NOTA/SAIDA") != std::string::npos ||
                linha.find("NOTA PROMISSORIA          Num:") != std::string::npos ||
                linha.find("T I C K    D E    C O N F E R E N C I A") != std::string::npos ||
                linha.find("Referencia       Quant/OE JaEntregue    Saldo/OE") != std::string::npos ||
                linha.find("Quant Un CItem-Descricao/Produto             VlTotal") != std::string::npos ||
                linha.find("          EAN     Quant Un  ValorUnit ValorTotal") != std::string::npos ||
                linha.find("1 VIA-CLIENTE") != std::string::npos ||
                linha.find("2 VIA-ESTABELECIMENTO") != std::string::npos ||
                linha.find("TD-Tipo/Documento   QDoc    ValorDoc   ValorPago") != std::string::npos ||
                linha.find("+-------- FECHAMENTO DE CAIXA ") != std::string::npos) {
                termica = 1;
            }

            linhas.push_back(linha);
        }
        arquivo.close();

        std::ofstream arquivoSaida(argv[1], std::ios::trunc);
        for (const auto& linhaLimpa : linhas) {
            arquivoSaida << linhaLimpa << '\n';
        }
        arquivoSaida.close();

        if (argc == 4 &&
            std::string(argv[2]) == "140" &&
            std::string(argv[3]) == "/PRE/VER/SEL") {

            RetiraULinha(argv[1]);
            std::string cmd = "NODOSIMP2 " + std::string(argv[1]) + " 140 /PRE/VER/SEL";
            system(cmd.c_str());

        } else if (termica == 1) {
            RetiraULinha(argv[1]);
            imptermica(argv[1]);   // <<< impressao GDI direta

        } else {
            RetiraULinha(argv[1]);

            if (fs::exists("C:\\WINPRINT\\MATRICIAL")) {
                std::string cmd = "cmd /C copy " + std::string(argv[1]) + " LPT1 /Y";
                system(cmd.c_str());
            } else {
                if (fs::exists(argv[1])) {
                    char exeFullPath[MAX_PATH];
                    GetModuleFileNameA(NULL, exeFullPath, MAX_PATH);

                    fs::path exePath = exeFullPath;
                    fs::path exeDir  = exePath.parent_path();
                    fs::path selFile = exeDir / "NODOSIMP.SEL";

                    std::string cmd = "NODOSIMP2 " + std::string(argv[1]) + " 140";

                    if (fs::exists(selFile)) {
                        cmd += " /SEL";
                    }

                    system(cmd.c_str());
                }
            }
        }
    } catch (const std::exception& e) {
        std::cout << "CONTATE AM SOFTware (75) 98845-1673 ERRO:" << e.what() << std::endl;
    }

    return 0;
}