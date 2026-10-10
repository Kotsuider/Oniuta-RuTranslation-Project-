#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// -------------------------------------------------------------------------------------------------
// Константы и глобальные переменные
// -------------------------------------------------------------------------------------------------
#define TARGET_IAT_CREATEFONT_A    ((void*)0x005E506C)  // Ячейка в RealLive.exe для CreateFontA
#define TARGET_IAT_SETWINDOWTEXT_A ((void*)0x005E5474)  // Ячейка в RealLive.exe для SetWindowTextA
#define TARGET_IAT_MESSAGEBOX_A    ((void*)0x005E5328)  // Ячейка в RealLive.exe для MessageBoxA

static HMODULE g_hRealVersion = NULL;
static char g_FontName[128] = "YasuSakuutaModern";
static DWORD g_TargetCharSet = 204;   // RUSSIAN_CHARSET
static int g_EnableSubst = 0;         // Подмена символов выключена по умолчанию
static int g_HookCreateFontCount = 0;
static int g_HookGlyphCount = 0;

// Таблица подмены в UTF-16 (wchar_t):
static wchar_t g_SubstSrcW[128] = L"><+%#^@;/";
static wchar_t g_SubstDstW[128] = L"ÓÕ¹²É«»{}";

// Оригинальные функции CreateFontA
typedef HFONT (WINAPI *pfnCreateFontA)(
    int cHeight, int cWidth, int cEscapement, int cOrientation, int cWeight,
    DWORD bItalic, DWORD bUnderline, DWORD bStrikeOut, DWORD iCharSet,
    DWORD iOutPrecision, DWORD iClipPrecision, DWORD iQuality,
    DWORD iPitchAndFamily, LPCSTR pszFaceName
);
static pfnCreateFontA g_pfnOrigCreateFontA = NULL;

// Оригинальная GetGlyphOutlineA / W
typedef DWORD (WINAPI *pfnGetGlyphOutlineA)(
    HDC hdc, UINT uChar, UINT fuFormat, LPGLYPHMETRICS lpgm,
    DWORD cjBuffer, LPVOID pvBuffer, const MAT2 *lpmat2
);
typedef DWORD (WINAPI *pfnGetGlyphOutlineW)(
    HDC hdc, UINT uChar, UINT fuFormat, LPGLYPHMETRICS lpgm,
    DWORD cjBuffer, LPVOID pvBuffer, const MAT2 *lpmat2
);
static pfnGetGlyphOutlineA g_pfnOrigGetGlyphOutlineA = NULL;
static pfnGetGlyphOutlineW g_pfnOrigGetGlyphOutlineW = NULL;

// Оригинальные SetWindowTextA и MessageBoxA
typedef BOOL (WINAPI *pfnSetWindowTextA)(HWND hWnd, LPCSTR lpString);
typedef int  (WINAPI *pfnMessageBoxA)(HWND hWnd, LPCSTR lpText, LPCSTR lpCaption, UINT uType);
static pfnSetWindowTextA g_pfnOrigSetWindowTextA = NULL;
static pfnMessageBoxA   g_pfnOrigMessageBoxA   = NULL;

// -------------------------------------------------------------------------------------------------
// Экспорты системной version.dll (все 17 функций)
// -------------------------------------------------------------------------------------------------
typedef BOOL  (WINAPI *pfnGetFileVersionInfoA)(LPCSTR, DWORD, DWORD, LPVOID);
typedef BOOL  (WINAPI *pfnGetFileVersionInfoW)(LPCWSTR, DWORD, DWORD, LPVOID);
typedef DWORD (WINAPI *pfnGetFileVersionInfoByHandle)(DWORD, HANDLE, LPVOID*, PDWORD);
typedef BOOL  (WINAPI *pfnGetFileVersionInfoExA)(DWORD, LPCSTR, DWORD, DWORD, LPVOID);
typedef BOOL  (WINAPI *pfnGetFileVersionInfoExW)(DWORD, LPCWSTR, DWORD, DWORD, LPVOID);
typedef DWORD (WINAPI *pfnGetFileVersionInfoSizeA)(LPCSTR, LPDWORD);
typedef DWORD (WINAPI *pfnGetFileVersionInfoSizeW)(LPCWSTR, LPDWORD);
typedef DWORD (WINAPI *pfnGetFileVersionInfoSizeExA)(DWORD, LPCSTR, LPDWORD);
typedef DWORD (WINAPI *pfnGetFileVersionInfoSizeExW)(DWORD, LPCWSTR, LPDWORD);
typedef DWORD (WINAPI *pfnVerFindFileA)(DWORD, LPCSTR, LPCSTR, LPCSTR, LPSTR, PUINT, LPSTR, PUINT);
typedef DWORD (WINAPI *pfnVerFindFileW)(DWORD, LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR, PUINT, LPWSTR, PUINT);
typedef DWORD (WINAPI *pfnVerInstallFileA)(DWORD, LPCSTR, LPCSTR, LPCSTR, LPCSTR, LPCSTR, LPSTR, PUINT);
typedef DWORD (WINAPI *pfnVerInstallFileW)(DWORD, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR, PUINT);
typedef DWORD (WINAPI *pfnVerLanguageNameA)(DWORD, LPSTR, DWORD);
typedef DWORD (WINAPI *pfnVerLanguageNameW)(DWORD, LPWSTR, DWORD);
typedef BOOL  (WINAPI *pfnVerQueryValueA)(LPCVOID, LPCSTR, LPVOID*, PUINT);
typedef BOOL  (WINAPI *pfnVerQueryValueW)(LPCVOID, LPCWSTR, LPVOID*, PUINT);

static pfnGetFileVersionInfoA        g_pfnRealGetFileVersionInfoA = NULL;
static pfnGetFileVersionInfoW        g_pfnRealGetFileVersionInfoW = NULL;
static pfnGetFileVersionInfoByHandle g_pfnRealGetFileVersionInfoByHandle = NULL;
static pfnGetFileVersionInfoExA      g_pfnRealGetFileVersionInfoExA = NULL;
static pfnGetFileVersionInfoExW      g_pfnRealGetFileVersionInfoExW = NULL;
static pfnGetFileVersionInfoSizeA    g_pfnRealGetFileVersionInfoSizeA = NULL;
static pfnGetFileVersionInfoSizeW    g_pfnRealGetFileVersionInfoSizeW = NULL;
static pfnGetFileVersionInfoSizeExA  g_pfnRealGetFileVersionInfoSizeExA = NULL;
static pfnGetFileVersionInfoSizeExW  g_pfnRealGetFileVersionInfoSizeExW = NULL;
static pfnVerFindFileA               g_pfnRealVerFindFileA = NULL;
static pfnVerFindFileW               g_pfnRealVerFindFileW = NULL;
static pfnVerInstallFileA            g_pfnRealVerInstallFileA = NULL;
static pfnVerInstallFileW            g_pfnRealVerInstallFileW = NULL;
static pfnVerLanguageNameA           g_pfnRealVerLanguageNameA = NULL;
static pfnVerLanguageNameW           g_pfnRealVerLanguageNameW = NULL;
static pfnVerQueryValueA             g_pfnRealVerQueryValueA = NULL;
static pfnVerQueryValueW             g_pfnRealVerQueryValueW = NULL;

// -------------------------------------------------------------------------------------------------
// Логирование в OutputDebugString
// -------------------------------------------------------------------------------------------------
static void LogDebug(const char* format, ...) {
    char buffer[512];
    va_list args;
    va_start(args, format);
    wvsprintfA(buffer, format, args);
    va_end(args);

    OutputDebugStringA("[FontFix] ");
    OutputDebugStringA(buffer);
    OutputDebugStringA("\n");
}

// -------------------------------------------------------------------------------------------------
// Вспомогательная функция: подмена одного символа wchar_t
// -------------------------------------------------------------------------------------------------
static wchar_t SubstituteWideChar(wchar_t wChar) {
    if (!g_EnableSubst) return wChar;

    size_t mapLen = wcslen(g_SubstSrcW);
    for (size_t i = 0; i < mapLen; i++) {
        if (g_SubstSrcW[i] == wChar) {
            return g_SubstDstW[i];
        }
    }
    return wChar;
}

// -------------------------------------------------------------------------------------------------
// Декодер RU_F обратно в русский Unicode (wchar_t)
// -------------------------------------------------------------------------------------------------
static wchar_t DecodeRU_F_Char(char c) {
    switch (c) {
        // Заглавные
        case 'A': return L'А'; case 'B': return L'Б'; case 'C': return L'В';
        case 'D': return L'Г'; case 'E': return L'Д'; case 'F': return L'Е';
        case 'G': return L'Ж'; case 'H': return L'З'; case 'I': return L'И';
        case 'J': return L'Й'; case 'K': return L'К'; case 'L': return L'Л';
        case 'M': return L'М'; case 'N': return L'Н'; case 'O': return L'О';
        case 'P': return L'П'; case 'Q': return L'Р'; case 'R': return L'С';
        case 'S': return L'Т'; case 'T': return L'У'; case 'U': return L'Ф';
        case 'V': return L'Х'; case 'W': return L'Ц'; case 'X': return L'Ч';
        case 'Y': return L'Ш'; case 'Z': return L'Щ';
        // Строчные
        case 'a': return L'а'; case 'b': return L'б'; case 'c': return L'в';
        case 'd': return L'г'; case 'e': return L'д'; case 'f': return L'е';
        case 'g': return L'ж'; case 'h': return L'з'; case 'i': return L'и';
        case 'j': return L'й'; case 'k': return L'к'; case 'l': return L'л';
        case 'm': return L'м'; case 'n': return L'н'; case 'o': return L'о';
        case 'p': return L'п'; case 'q': return L'р'; case 'r': return L'с';
        case 's': return L'т'; case 't': return L'у'; case 'u': return L'ф';
        case 'v': return L'х'; case 'w': return L'ц'; case 'x': return L'ч';
        case 'y': return L'ш'; case 'z': return L'щ';
        // Спецсимволы RU_F
        case '[': return L'Ъ'; case ']': return L'Ь'; case '`': return L'ё';
        case '#': return L'э'; case '|': return L'ы'; case '%': return L'я';
        case (char)0xa1: return L'Ы'; case '&': return L'ь'; case '+': return L'ъ';
        case '-': return L'Ю'; case '$': return L'ю'; case '^': return L'Я';
        case '*': return L'Ё'; case '@': return L'Э'; case '=': return L'—';
        default: return (wchar_t)(unsigned char)c;
    }
}

static void DecodeRU_F_String(const char* src, wchar_t* dst, int maxLen) {
    if (!src || !dst || maxLen <= 0) return;
    int outIdx = 0;
    while (*src && outIdx < maxLen - 1) {
        dst[outIdx++] = DecodeRU_F_Char(*src++);
    }
    dst[outIdx] = L'\0';
}

// -------------------------------------------------------------------------------------------------
// Хуки GetGlyphOutlineW и GetGlyphOutlineA
// -------------------------------------------------------------------------------------------------
DWORD WINAPI HookedGetGlyphOutlineW(
    HDC hdc, UINT uChar, UINT fuFormat, LPGLYPHMETRICS lpgm,
    DWORD cjBuffer, LPVOID pvBuffer, const MAT2 *lpmat2
) {
    g_HookGlyphCount++;
    wchar_t originalW = (wchar_t)uChar;
    wchar_t targetW = SubstituteWideChar(originalW);

    if (targetW != originalW) {
        LogDebug("GetGlyphOutlineW: substituted 0x%04X ('%c') -> 0x%04X ('%c')",
            (unsigned)originalW, (char)originalW, (unsigned)targetW, (char)targetW);
    }

    if (!g_pfnOrigGetGlyphOutlineW) {
        HMODULE hGdi = GetModuleHandleA("GDI32.dll");
        g_pfnOrigGetGlyphOutlineW = (pfnGetGlyphOutlineW)GetProcAddress(hGdi, "GetGlyphOutlineW");
    }

    return g_pfnOrigGetGlyphOutlineW(hdc, (UINT)targetW, fuFormat, lpgm, cjBuffer, pvBuffer, lpmat2);
}

DWORD WINAPI HookedGetGlyphOutlineA(
    HDC hdc, UINT uChar, UINT fuFormat, LPGLYPHMETRICS lpgm,
    DWORD cjBuffer, LPVOID pvBuffer, const MAT2 *lpmat2
) {
    g_HookGlyphCount++;

    char a[3] = { 0 };
    if (uChar < 0x100) {
        a[0] = (char)uChar;
    } else {
        a[0] = (char)(uChar >> 8);
        a[1] = (char)uChar;
    }

    wchar_t wBuf[2] = { 0 };
    MultiByteToWideChar(932 /* CP_SHIFTJIS */, 0, a, -1, wBuf, 2);
    if (!wBuf[0]) {
        wBuf[0] = (wchar_t)uChar;
    }

    wchar_t originalW = wBuf[0];
    wchar_t targetW = SubstituteWideChar(originalW);

    if (targetW != originalW) {
        LogDebug("GetGlyphOutlineA: substituted 0x%02X ('%c') -> 0x%04X",
            uChar, (char)uChar, (unsigned)targetW);
    }

    return HookedGetGlyphOutlineW(hdc, (UINT)targetW, fuFormat, lpgm, cjBuffer, pvBuffer, lpmat2);
}

// -------------------------------------------------------------------------------------------------
// Хук CreateFontA
// -------------------------------------------------------------------------------------------------
HFONT WINAPI HookedCreateFontA(
    int cHeight, int cWidth, int cEscapement, int cOrientation, int cWeight,
    DWORD bItalic, DWORD bUnderline, DWORD bStrikeOut, DWORD iCharSet,
    DWORD iOutPrecision, DWORD iClipPrecision, DWORD iQuality,
    DWORD iPitchAndFamily, LPCSTR pszFaceName
) {
    g_HookCreateFontCount++;

    DWORD newCharSet = g_TargetCharSet;
    LPCSTR newFontName = g_FontName;

    HFONT hFont = g_pfnOrigCreateFontA(
        cHeight, cWidth, cEscapement, cOrientation, cWeight,
        bItalic, bUnderline, bStrikeOut, newCharSet,
        iOutPrecision, iClipPrecision, iQuality,
        iPitchAndFamily, newFontName
    );

    HDC hdc = CreateCompatibleDC(NULL);
    if (hdc) {
        HGDIOBJ hOld = SelectObject(hdc, hFont);
        char faceName[64] = {0};
        GetTextFaceA(hdc, sizeof(faceName), faceName);
        SelectObject(hdc, hOld);
        DeleteDC(hdc);

        LogDebug("CreateFontA #%d: requested '%s' -> applied '%s' (CharSet=%u) hFont=%p",
            g_HookCreateFontCount, pszFaceName ? pszFaceName : "NULL", faceName, (unsigned)newCharSet, hFont);
    }

    return hFont;
}

// -------------------------------------------------------------------------------------------------
// Хук SetWindowTextA (преобразует заголовок в красивый русский Unicode)
// -------------------------------------------------------------------------------------------------
BOOL WINAPI HookedSetWindowTextA(HWND hWnd, LPCSTR lpString) {
    if (!lpString) {
        return SetWindowTextW(hWnd, L"");
    }

    LogDebug("SetWindowTextA: '%s'", lpString);

    const char* p = lpString;
    // Удаляем японский или латинский префикс названия игры
    if (strncmp(p, "Oni Uta", 7) == 0) {
        p += 7;
    } else if (strncmp(p, "\x8b\x53\x82\xa4\x82\xbd\x81\x42", 8) == 0) { // "鬼うた。" (Shift-JIS)
        p += 8;
    } else if (strncmp(p, "\x8b\x53", 2) == 0) { // "鬼" (Shift-JIS)
        p += 2;
    }

    // Пропускаем разделители, пробелы, слэши и полноширинные пробелы в начале
    while (*p) {
        if (*p == ' ' || *p == '\t' || *p == '-' || *p == '=' || *p == '\\') {
            p++;
        } else if ((unsigned char)p[0] == 0x81 && (unsigned char)p[1] == 0x40) { // Shift-JIS fullwidth space
            p += 2;
        } else if ((unsigned char)p[0] == 0xa1 && (unsigned char)p[1] == 0xa1) { // EUC-JP fullwidth space
            p += 2;
        } else {
            break;
        }
    }

    wchar_t wSub[512] = {0};
    DecodeRU_F_String(p, wSub, 512);

    // Удаляем ведущие пробелы в декодированной подстроке
    wchar_t* pSub = wSub;
    while (*pSub == L' ' || *pSub == L'\t' || *pSub == L'\\' || *pSub == 0x3000) {
        pSub++;
    }

    wchar_t wFinal[1024] = {0};
    if (pSub[0]) {
        wsprintfW(wFinal, L"Песнь Они — %s", pSub);
    } else {
        lstrcpyW(wFinal, L"Песнь Они");
    }

    LogDebug("SetWindowTextA applied: '%S'", wFinal);
    return SetWindowTextW(hWnd, wFinal);
}

// -------------------------------------------------------------------------------------------------
// Хук MessageBoxA (переводит диалог выхода и системные сообщения в русский Unicode)
// -------------------------------------------------------------------------------------------------
int WINAPI HookedMessageBoxA(HWND hWnd, LPCSTR lpText, LPCSTR lpCaption, UINT uType) {
    LogDebug("MessageBoxA: text='%s', caption='%s'", lpText ? lpText : "NULL", lpCaption ? lpCaption : "NULL");

    wchar_t wText[1024] = {0};
    wchar_t wCaption[256] = L"Песнь Они";

    if (lpText) {
        if (strstr(lpText, "Vyyti") || strstr(lpText, "\x8f\x49\x97\xb9") || strstr(lpText, "\x83\x51\x81\x5b\x83\x80")) {
            lstrcpyW(wText, L"Вы действительно хотите выйти?");
        } else if (strstr(lpText, "\x83\x5e\x83\x43\x83\x67\x83\x8b") || strstr(lpText, "Title") || strstr(lpText, "title")) {
            lstrcpyW(wText, L"Вернуться в главное меню?");
        } else {
            DecodeRU_F_String(lpText, wText, 1024);
        }
    }

    return MessageBoxW(hWnd, wText, wCaption, uType);
}

// -------------------------------------------------------------------------------------------------
// Поиск и хук функции в IAT модуля
// -------------------------------------------------------------------------------------------------
static int HookIAT(HMODULE hModule, const char* targetDll, const char* funcName, void* newFunc, void** origFunc) {
    if (!hModule) return 0;

    PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)hModule;
    if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) return 0;

    PIMAGE_NT_HEADERS ntHeaders = (PIMAGE_NT_HEADERS)((BYTE*)hModule + dosHeader->e_lfanew);
    if (ntHeaders->Signature != IMAGE_NT_SIGNATURE) return 0;

    IMAGE_DATA_DIRECTORY importDir = ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (importDir.Size == 0) return 0;

    PIMAGE_IMPORT_DESCRIPTOR importDesc = (PIMAGE_IMPORT_DESCRIPTOR)((BYTE*)hModule + importDir.VirtualAddress);

    for (; importDesc->Name != 0; importDesc++) {
        const char* modName = (const char*)((BYTE*)hModule + importDesc->Name);
        if (_stricmp(modName, targetDll) == 0) {
            PIMAGE_THUNK_DATA thunkOriginal = (PIMAGE_THUNK_DATA)((BYTE*)hModule + importDesc->OriginalFirstThunk);
            PIMAGE_THUNK_DATA thunkIAT = (PIMAGE_THUNK_DATA)((BYTE*)hModule + importDesc->FirstThunk);

            if (!importDesc->OriginalFirstThunk) {
                thunkOriginal = thunkIAT;
            }

            for (; thunkIAT->u1.Function != 0; thunkIAT++, thunkOriginal++) {
                if (IMAGE_SNAP_BY_ORDINAL(thunkOriginal->u1.Ordinal)) continue;

                PIMAGE_IMPORT_BY_NAME importByName = (PIMAGE_IMPORT_BY_NAME)((BYTE*)hModule + thunkOriginal->u1.AddressOfData);
                if (strcmp((const char*)importByName->Name, funcName) == 0) {
                    DWORD oldProtect;
                    if (VirtualProtect(&thunkIAT->u1.Function, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect)) {
                        if (origFunc && !*origFunc) {
                            *origFunc = (void*)thunkIAT->u1.Function;
                        }
                        thunkIAT->u1.Function = (DWORD)newFunc;
                        VirtualProtect(&thunkIAT->u1.Function, sizeof(void*), oldProtect, &oldProtect);
                        LogDebug("HookIAT: HOOKED %s!%s in %p (IAT slot: %p)", targetDll, funcName, hModule, &thunkIAT->u1.Function);
                        return 1;
                    }
                }
            }
        }
    }
    return 0;
}

// -------------------------------------------------------------------------------------------------
// Поток-наблюдатель (WatchThread)
// -------------------------------------------------------------------------------------------------
static DWORD WINAPI WatchThread(LPVOID lpParam) {
    DWORD oldProtect;
    for (int i = 0; i < 200; i++) {
        Sleep(50); // 10 секунд

        if (*(void**)TARGET_IAT_CREATEFONT_A != (void*)HookedCreateFontA) {
            LogDebug("WatchThread[%d]: restoring CreateFontA hook", i);

            if (VirtualProtect(TARGET_IAT_CREATEFONT_A, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect)) {
                *(void**)TARGET_IAT_CREATEFONT_A = (void*)HookedCreateFontA;
                VirtualProtect(TARGET_IAT_CREATEFONT_A, sizeof(void*), oldProtect, &oldProtect);
            }
        }

        if (*(void**)TARGET_IAT_SETWINDOWTEXT_A != (void*)HookedSetWindowTextA) {
            if (VirtualProtect(TARGET_IAT_SETWINDOWTEXT_A, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect)) {
                *(void**)TARGET_IAT_SETWINDOWTEXT_A = (void*)HookedSetWindowTextA;
                VirtualProtect(TARGET_IAT_SETWINDOWTEXT_A, sizeof(void*), oldProtect, &oldProtect);
            }
        }

        if (*(void**)TARGET_IAT_MESSAGEBOX_A != (void*)HookedMessageBoxA) {
            if (VirtualProtect(TARGET_IAT_MESSAGEBOX_A, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect)) {
                *(void**)TARGET_IAT_MESSAGEBOX_A = (void*)HookedMessageBoxA;
                VirtualProtect(TARGET_IAT_MESSAGEBOX_A, sizeof(void*), oldProtect, &oldProtect);
            }
        }
    }

    LogDebug("WatchThread: done, CreateFont=%d, Glyphs=%d", g_HookCreateFontCount, g_HookGlyphCount);
    return 0;
}

// -------------------------------------------------------------------------------------------------
// Реализация прокси-функций системной version.dll
// -------------------------------------------------------------------------------------------------
BOOL WINAPI Proxy_GetFileVersionInfoA(LPCSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData) {
    if (!g_pfnRealGetFileVersionInfoA) return FALSE;
    return g_pfnRealGetFileVersionInfoA(lptstrFilename, dwHandle, dwLen, lpData);
}

BOOL WINAPI Proxy_GetFileVersionInfoW(LPCWSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData) {
    if (!g_pfnRealGetFileVersionInfoW) return FALSE;
    return g_pfnRealGetFileVersionInfoW(lptstrFilename, dwHandle, dwLen, lpData);
}

DWORD WINAPI Proxy_GetFileVersionInfoByHandle(DWORD dwFlags, HANDLE hFile, LPVOID *lplpData, PDWORD pdwLen) {
    if (!g_pfnRealGetFileVersionInfoByHandle) return 0;
    return g_pfnRealGetFileVersionInfoByHandle(dwFlags, hFile, lplpData, pdwLen);
}

BOOL WINAPI Proxy_GetFileVersionInfoExA(DWORD dwFlags, LPCSTR lpwstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData) {
    if (!g_pfnRealGetFileVersionInfoExA) return FALSE;
    return g_pfnRealGetFileVersionInfoExA(dwFlags, lpwstrFilename, dwHandle, dwLen, lpData);
}

BOOL WINAPI Proxy_GetFileVersionInfoExW(DWORD dwFlags, LPCWSTR lpwstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData) {
    if (!g_pfnRealGetFileVersionInfoExW) return FALSE;
    return g_pfnRealGetFileVersionInfoExW(dwFlags, lpwstrFilename, dwHandle, dwLen, lpData);
}

DWORD WINAPI Proxy_GetFileVersionInfoSizeA(LPCSTR lptstrFilename, LPDWORD lpdwHandle) {
    if (!g_pfnRealGetFileVersionInfoSizeA) return 0;
    return g_pfnRealGetFileVersionInfoSizeA(lptstrFilename, lpdwHandle);
}

DWORD WINAPI Proxy_GetFileVersionInfoSizeW(LPCWSTR lptstrFilename, LPDWORD lpdwHandle) {
    if (!g_pfnRealGetFileVersionInfoSizeW) return 0;
    return g_pfnRealGetFileVersionInfoSizeW(lptstrFilename, lpdwHandle);
}

DWORD WINAPI Proxy_GetFileVersionInfoSizeExA(DWORD dwFlags, LPCSTR lpwstrFilename, LPDWORD lpdwHandle) {
    if (!g_pfnRealGetFileVersionInfoSizeExA) return 0;
    return g_pfnRealGetFileVersionInfoSizeExA(dwFlags, lpwstrFilename, lpdwHandle);
}

DWORD WINAPI Proxy_GetFileVersionInfoSizeExW(DWORD dwFlags, LPCWSTR lpwstrFilename, LPDWORD lpdwHandle) {
    if (!g_pfnRealGetFileVersionInfoSizeExW) return 0;
    return g_pfnRealGetFileVersionInfoSizeExW(dwFlags, lpwstrFilename, lpdwHandle);
}

DWORD WINAPI Proxy_VerFindFileA(DWORD uFlags, LPCSTR szFileName, LPCSTR szWinDir, LPCSTR szAppDir, LPSTR szCurDir, PUINT lpuCurDirLen, LPSTR szDestDir, PUINT lpuDestDirLen) {
    if (!g_pfnRealVerFindFileA) return 0;
    return g_pfnRealVerFindFileA(uFlags, szFileName, szWinDir, szAppDir, szCurDir, lpuCurDirLen, szDestDir, lpuDestDirLen);
}

DWORD WINAPI Proxy_VerFindFileW(DWORD uFlags, LPCWSTR szFileName, LPCWSTR szWinDir, LPCWSTR szAppDir, LPWSTR szCurDir, PUINT lpuCurDirLen, LPWSTR szDestDir, PUINT lpuDestDirLen) {
    if (!g_pfnRealVerFindFileW) return 0;
    return g_pfnRealVerFindFileW(uFlags, szFileName, szWinDir, szAppDir, szCurDir, lpuCurDirLen, szDestDir, lpuDestDirLen);
}

DWORD WINAPI Proxy_VerInstallFileA(DWORD uFlags, LPCSTR szSrcFileName, LPCSTR szDestFileName, LPCSTR szSrcDir, LPCSTR szDestDir, LPCSTR szCurDir, LPSTR szTmpFile, PUINT lpuTmpFileLen) {
    if (!g_pfnRealVerInstallFileA) return 0;
    return g_pfnRealVerInstallFileA(uFlags, szSrcFileName, szDestFileName, szSrcDir, szDestDir, szCurDir, szTmpFile, lpuTmpFileLen);
}

DWORD WINAPI Proxy_VerInstallFileW(DWORD uFlags, LPCWSTR szSrcFileName, LPCWSTR szDestFileName, LPCWSTR szSrcDir, LPCWSTR szDestDir, LPCWSTR szCurDir, LPWSTR szTmpFile, PUINT lpuTmpFileLen) {
    if (!g_pfnRealVerInstallFileW) return 0;
    return g_pfnRealVerInstallFileW(uFlags, szSrcFileName, szDestFileName, szSrcDir, szDestDir, szCurDir, szTmpFile, lpuTmpFileLen);
}

DWORD WINAPI Proxy_VerLanguageNameA(DWORD wLang, LPSTR szLang, DWORD cchLang) {
    if (!g_pfnRealVerLanguageNameA) return 0;
    return g_pfnRealVerLanguageNameA(wLang, szLang, cchLang);
}

DWORD WINAPI Proxy_VerLanguageNameW(DWORD wLang, LPWSTR szLang, DWORD cchLang) {
    if (!g_pfnRealVerLanguageNameW) return 0;
    return g_pfnRealVerLanguageNameW(wLang, szLang, cchLang);
}

BOOL WINAPI Proxy_VerQueryValueA(LPCVOID pBlock, LPCSTR lpSubBlock, LPVOID *lplpBuffer, PUINT puLen) {
    if (!g_pfnRealVerQueryValueA) return FALSE;
    return g_pfnRealVerQueryValueA(pBlock, lpSubBlock, lplpBuffer, puLen);
}

BOOL WINAPI Proxy_VerQueryValueW(LPCVOID pBlock, LPCWSTR lpSubBlock, LPVOID *lplpBuffer, PUINT puLen) {
    if (!g_pfnRealVerQueryValueW) return FALSE;
    return g_pfnRealVerQueryValueW(pBlock, lpSubBlock, lplpBuffer, puLen);
}

// -------------------------------------------------------------------------------------------------
// Точка входа библиотеки (DllMain)
// -------------------------------------------------------------------------------------------------
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinstDLL);
        LogDebug("DllMain: ATTACH (PID=%u)", GetCurrentProcessId());

        // 1. Загрузка системной version.dll
        char sysPath[MAX_PATH];
        GetSystemDirectoryA(sysPath, MAX_PATH);
        lstrcatA(sysPath, "\\version.dll");

        g_hRealVersion = LoadLibraryA(sysPath);
        if (g_hRealVersion) {
            g_pfnRealGetFileVersionInfoA        = (pfnGetFileVersionInfoA)GetProcAddress(g_hRealVersion, "GetFileVersionInfoA");
            g_pfnRealGetFileVersionInfoW        = (pfnGetFileVersionInfoW)GetProcAddress(g_hRealVersion, "GetFileVersionInfoW");
            g_pfnRealGetFileVersionInfoByHandle = (pfnGetFileVersionInfoByHandle)GetProcAddress(g_hRealVersion, "GetFileVersionInfoByHandle");
            g_pfnRealGetFileVersionInfoExA      = (pfnGetFileVersionInfoExA)GetProcAddress(g_hRealVersion, "GetFileVersionInfoExA");
            g_pfnRealGetFileVersionInfoExW      = (pfnGetFileVersionInfoExW)GetProcAddress(g_hRealVersion, "GetFileVersionInfoExW");
            g_pfnRealGetFileVersionInfoSizeA    = (pfnGetFileVersionInfoSizeA)GetProcAddress(g_hRealVersion, "GetFileVersionInfoSizeA");
            g_pfnRealGetFileVersionInfoSizeW    = (pfnGetFileVersionInfoSizeW)GetProcAddress(g_hRealVersion, "GetFileVersionInfoSizeW");
            g_pfnRealGetFileVersionInfoSizeExA  = (pfnGetFileVersionInfoSizeExA)GetProcAddress(g_hRealVersion, "GetFileVersionInfoSizeExA");
            g_pfnRealGetFileVersionInfoSizeExW  = (pfnGetFileVersionInfoSizeExW)GetProcAddress(g_hRealVersion, "GetFileVersionInfoSizeExW");
            g_pfnRealVerFindFileA               = (pfnVerFindFileA)GetProcAddress(g_hRealVersion, "VerFindFileA");
            g_pfnRealVerFindFileW               = (pfnVerFindFileW)GetProcAddress(g_hRealVersion, "VerFindFileW");
            g_pfnRealVerInstallFileA            = (pfnVerInstallFileA)GetProcAddress(g_hRealVersion, "VerInstallFileA");
            g_pfnRealVerInstallFileW            = (pfnVerInstallFileW)GetProcAddress(g_hRealVersion, "VerInstallFileW");
            g_pfnRealVerLanguageNameA           = (pfnVerLanguageNameA)GetProcAddress(g_hRealVersion, "VerLanguageNameA");
            g_pfnRealVerLanguageNameW           = (pfnVerLanguageNameW)GetProcAddress(g_hRealVersion, "VerLanguageNameW");
            g_pfnRealVerQueryValueA             = (pfnVerQueryValueA)GetProcAddress(g_hRealVersion, "VerQueryValueA");
            g_pfnRealVerQueryValueW             = (pfnVerQueryValueW)GetProcAddress(g_hRealVersion, "VerQueryValueW");
        }

        // 2. Чтение настроек из fontfix.ini
        char iniPath[MAX_PATH];
        GetModuleFileNameA(NULL, iniPath, MAX_PATH);
        char* pSlash = strrchr(iniPath, '\\');
        if (pSlash) {
            lstrcpyA(pSlash + 1, "fontfix.ini");
        }

        GetPrivateProfileStringA("FontFix", "FontName", "YasuSakuutaModern", g_FontName, sizeof(g_FontName), iniPath);
        g_TargetCharSet = GetPrivateProfileIntA("FontFix", "CharSet", 204, iniPath);
        g_EnableSubst   = GetPrivateProfileIntA("FontFix", "Subst", 0, iniPath);

        char customSrc[128] = {0};
        char customDst[128] = {0};
        GetPrivateProfileStringA("FontFix", "SourceChars", "", customSrc, sizeof(customSrc), iniPath);
        GetPrivateProfileStringA("FontFix", "TargetChars", "", customDst, sizeof(customDst), iniPath);
        if (customSrc[0] && customDst[0]) {
            MultiByteToWideChar(CP_UTF8, 0, customSrc, -1, g_SubstSrcW, sizeof(g_SubstSrcW)/sizeof(wchar_t));
            MultiByteToWideChar(CP_UTF8, 0, customDst, -1, g_SubstDstW, sizeof(g_SubstDstW)/sizeof(wchar_t));
        }

        LogDebug("Config: FontName='%s', CharSet=%u, Subst=%d", g_FontName, (unsigned)g_TargetCharSet, g_EnableSubst);

        // 3. Автоматическая регистрация TTF-шрифта
        char ttfPath[MAX_PATH];
        GetModuleFileNameA(NULL, ttfPath, MAX_PATH);
        pSlash = strrchr(ttfPath, '\\');
        if (pSlash) {
            wsprintfA(pSlash + 1, "%s.ttf", g_FontName);
            if (GetFileAttributesA(ttfPath) != INVALID_FILE_ATTRIBUTES) {
                int res = AddFontResourceExA(ttfPath, FR_PRIVATE, 0);
                LogDebug("AddFontResourceEx: '%s' loaded (code=%d)", ttfPath, res);
            }
        }

        // 4. Получение оригинальных адресов из GDI32.dll и USER32.dll
        HMODULE hGdi = GetModuleHandleA("GDI32.dll");
        if (hGdi) {
            g_pfnOrigCreateFontA      = (pfnCreateFontA)GetProcAddress(hGdi, "CreateFontA");
            g_pfnOrigGetGlyphOutlineA = (pfnGetGlyphOutlineA)GetProcAddress(hGdi, "GetGlyphOutlineA");
            g_pfnOrigGetGlyphOutlineW = (pfnGetGlyphOutlineW)GetProcAddress(hGdi, "GetGlyphOutlineW");
        }

        HMODULE hUser = GetModuleHandleA("USER32.dll");
        if (hUser) {
            g_pfnOrigSetWindowTextA   = (pfnSetWindowTextA)GetProcAddress(hUser, "SetWindowTextA");
            g_pfnOrigMessageBoxA      = (pfnMessageBoxA)GetProcAddress(hUser, "MessageBoxA");
        }

        // 5. Хуки по фиксированным адресам IAT
        DWORD oldProtect;
        if (VirtualProtect(TARGET_IAT_CREATEFONT_A, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect)) {
            *(void**)TARGET_IAT_CREATEFONT_A = (void*)HookedCreateFontA;
            VirtualProtect(TARGET_IAT_CREATEFONT_A, sizeof(void*), oldProtect, &oldProtect);
            LogDebug("CreateFontA hooked at %p", TARGET_IAT_CREATEFONT_A);
        }

        if (VirtualProtect(TARGET_IAT_SETWINDOWTEXT_A, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect)) {
            *(void**)TARGET_IAT_SETWINDOWTEXT_A = (void*)HookedSetWindowTextA;
            VirtualProtect(TARGET_IAT_SETWINDOWTEXT_A, sizeof(void*), oldProtect, &oldProtect);
            LogDebug("SetWindowTextA hooked at %p", TARGET_IAT_SETWINDOWTEXT_A);
        }

        if (VirtualProtect(TARGET_IAT_MESSAGEBOX_A, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect)) {
            *(void**)TARGET_IAT_MESSAGEBOX_A = (void*)HookedMessageBoxA;
            VirtualProtect(TARGET_IAT_MESSAGEBOX_A, sizeof(void*), oldProtect, &oldProtect);
            LogDebug("MessageBoxA hooked at %p", TARGET_IAT_MESSAGEBOX_A);
        }

        // 6. Хуки через IAT во всех модулях
        HMODULE hMainExe = GetModuleHandleA(NULL);
        HookIAT(hMainExe, "GDI32.dll", "GetGlyphOutlineA", (void*)HookedGetGlyphOutlineA, (void**)&g_pfnOrigGetGlyphOutlineA);
        HookIAT(hMainExe, "GDI32.dll", "GetGlyphOutlineW", (void*)HookedGetGlyphOutlineW, (void**)&g_pfnOrigGetGlyphOutlineW);
        HookIAT(hMainExe, "USER32.dll", "SetWindowTextA", (void*)HookedSetWindowTextA, (void**)&g_pfnOrigSetWindowTextA);
        HookIAT(hMainExe, "USER32.dll", "MessageBoxA", (void*)HookedMessageBoxA, (void**)&g_pfnOrigMessageBoxA);

        HMODULE hRlD3D = GetModuleHandleA("rl_d3d.dll");
        if (hRlD3D) {
            HookIAT(hRlD3D, "GDI32.dll", "GetGlyphOutlineA", (void*)HookedGetGlyphOutlineA, (void**)&g_pfnOrigGetGlyphOutlineA);
            HookIAT(hRlD3D, "GDI32.dll", "GetGlyphOutlineW", (void*)HookedGetGlyphOutlineW, (void**)&g_pfnOrigGetGlyphOutlineW);
            HookIAT(hRlD3D, "USER32.dll", "SetWindowTextA", (void*)HookedSetWindowTextA, (void**)&g_pfnOrigSetWindowTextA);
            HookIAT(hRlD3D, "USER32.dll", "MessageBoxA", (void*)HookedMessageBoxA, (void**)&g_pfnOrigMessageBoxA);
        }

        // 7. Запуск потока-стража
        HANDLE hThread = CreateThread(NULL, 0, WatchThread, NULL, 0, NULL);
        if (hThread) {
            CloseHandle(hThread);
        }

        LogDebug("DllMain: done");
    }
    else if (fdwReason == DLL_PROCESS_DETACH) {
        if (g_hRealVersion) {
            FreeLibrary(g_hRealVersion);
            g_hRealVersion = NULL;
        }
    }

    return TRUE;
}
