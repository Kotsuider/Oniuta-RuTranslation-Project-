#!/usr/bin/env python3
"""
utfdump.py — инструмент для работы с переводом .utf скриптов

Режимы:

  dump-xlsx <папка_с_utf/> <out.xlsx>
      Дамп всех .utf из папки в Excel таблицу.
      Каждый .utf = отдельный лист.
      Колонки: Row | Original JP | TL (RU) | TLC
        Row        — идентификатор строки (#character или <NNNN>)
        Original JP — исходный текст (или вся строка #character '…')
        TL (RU)    — основной перевод
        TLC        — финальная версия (приоритет над TL)
      Пустые строки и комментарии (// ...) не включаются.

  dump-xlsx <file.utf> <out.xlsx>
      То же, но для одного файла.

  from-xlsx <папка_с_utf/> <input.xlsx> <папка_вывода/>
      Конвертировать заполненный xlsx обратно в .utf файлы.
      Логика подстановки:
        - Если TLC не пусто  → использовать TLC
        - Иначе если TL не пусто → использовать TL
        - Иначе → строка не меняется (оригинал)
      Кириллица автоматически конвертируется в RU_F кодировку.
      Перенос строк выполняется автоматически.

  wrap <input.xlsx> <output.xlsx>
      Применить автоматический перенос строк к колонкам TL и TLC в xlsx.
      Обрабатывает только ячейки с кириллицей.
      Разделитель строк: \\n  Перенос слова: -- (среднее тире)
      Лимит: 48 символов на строку.

Формат .utf:
  UTF-8, строки разделены \\n (или \\r\\n).
  Структура:
    // комментарий            — пропускается
    пустая строка             — пропускается
    #character 'имя'          — строка персонажа (переводится)
    <NNNN> текст              — диалоговая/нарративная строка

RU_F кодировка (автоматически при from-xlsx):
  % = я   ^ = Я   @ = Э   * = Ё   + = ъ   - = Ю   = = —
  Любые тире в тексте (-, —, –) сводятся к –  перед RU_F-кодированием,
  затем – кодируется как =.
  Остальная кириллица: А→A, Б→B, В→C … и т.д. (как в оригинале).
"""

import sys, re
from pathlib import Path

# ── RU_F таблица ────────────────────────────────────────────────────────────

_RU_F_UPPER = {
    'A': 'А', 'B': 'Б', 'C': 'В', 'D': 'Г', 'E': 'Д', 'F': 'Е',
    'G': 'Ж', 'H': 'З', 'I': 'И', 'J': 'Й', 'K': 'К', 'L': 'Л',
    'M': 'М', 'N': 'Н', 'O': 'О', 'P': 'П', 'Q': 'Р', 'R': 'С',
    'S': 'Т', 'T': 'У', 'U': 'Ф', 'V': 'Х', 'W': 'Ц', 'X': 'Ч',
    'Y': 'Ш', 'Z': 'Щ',
}
_RU_F_LOWER = {k.lower(): v.lower() for k, v in _RU_F_UPPER.items()}

# Кириллица → Latin (основная таблица)
_CYR_TO_LATIN = {}
for _l, _c in _RU_F_UPPER.items():
    _CYR_TO_LATIN[_c] = _l
for _l, _c in _RU_F_LOWER.items():
    _CYR_TO_LATIN[_c] = _l

# Специальные символы RU_F для этого движка
# % = я   ^ = Я   @ = Э   * = Ё   + = ъ   - = Ю   = = —(тире)
# Все виды тире (-, —, –) нормализуются в – перед кодированием,
# затем – → =
_CYR_SPECIAL = {
    'Ъ': '[', 'Ь': ']', 'ё': '`', 'э': '#', 'ы': '|', 'я': '%',
    'Ы': '\xa1', 'ь': '&', 'ъ': '+', 'Ю': '-', 'ю': '$',
    'Я': '^', 'Ё': '*', 'Э': '@', 'Й': 'J', 'й': 'j', '«': '"','»': '"', '「': '"','」': '"',
    '„': '"', '“': '"', '”': '"', '‘': "'", '’': "'",
    '–': '=',   # en-dash (нормализованное тире) → =
}

_FULL_TABLE = {}
_FULL_TABLE.update(_CYR_TO_LATIN)
_FULL_TABLE.update(_CYR_SPECIAL)   # специальные перекрывают общую таблицу

# Нормализация тире: -, —, – → –
_DASH_RE = re.compile(r'[-\u2014\u2013]')   # ASCII-дефис, em-dash, en-dash

def _normalize_dashes(text):
    """Все виды тире заменить на – (en-dash), который потом → '='."""
    return _DASH_RE.sub('\u2013', text)


def apply_ru_f(text):
    """Перевести кириллицу в RU_F-кодировку."""
    if not text:
        return text
    text = _normalize_dashes(text)
    return ''.join(_FULL_TABLE.get(ch, ch) for ch in text)


# ── Разбор .utf ──────────────────────────────────────────────────────────────

_LINE_TAG_RE  = re.compile(r'^<(\d{4})>\s*(.*)', re.DOTALL)
_CHAR_RE      = re.compile(r'^#character\s+(.*)')
_COMMENT_RE   = re.compile(r'^//')


def read_utf_lines(path):
    """Читаем файл, возвращаем список строк (без финального \\n)."""
    text = path.read_text(encoding='utf-8')
    lines = text.splitlines()
    return lines


def get_translatable_rows(lines):
    """
    Возвращает список dict:
      line_index — индекс строки в файле
      row_id     — '<NNNN>' или '#character'
      text       — текст для перевода
    """
    result = []
    for i, line in enumerate(lines):
        stripped = line.rstrip()
        if not stripped or _COMMENT_RE.match(stripped):
            continue
        m_tag = _LINE_TAG_RE.match(stripped)
        if m_tag:
            result.append({
                'line_index': i,
                'row_id':     f'<{m_tag.group(1)}>',
                'text':       m_tag.group(2),
            })
            continue
        m_char = _CHAR_RE.match(stripped)
        if m_char:
            result.append({
                'line_index': i,
                'row_id':     '#character',
                'text':       m_char.group(1),
            })
    return result


# ── Перенос строк (wrap) ─────────────────────────────────────────────────────

WRAP_LIMIT  = 52
WRAP_HYPHEN = '='
_HAS_CYRILLIC = re.compile(r'[\u0430-\u044f\u0451\u0410-\u042f\u0401]')

_hyph = None


def _get_hyph():
    global _hyph
    if _hyph is not None:
        return _hyph
    try:
        import pyphen
        _hyph = pyphen.Pyphen(lang='ru')
    except ImportError:
        print("ВНИМАНИЕ: pyphen не установлен, перенос по слогам недоступен.")
        print("  Установите: pip install pyphen")
        _hyph = False
    return _hyph


def _syllable_positions(word):
    h = _get_hyph()
    if h:
        return h.positions(word)
    return []


def _split_long_word(word, limit, out_lines):
    remaining = word
    while len(remaining) > limit:
        positions = _syllable_positions(remaining)
        best = None
        for p in positions:
            if p + 1 <= limit:
                best = p
        if best is None:
            best = limit - 1
        out_lines.append(remaining[:best] + WRAP_HYPHEN)
        remaining = remaining[best:]
    return remaining


def wrap_text(text, limit=WRAP_LIMIT):
    """
    Перенос текста по словам/слогам.
    Только для строк с кириллицей.
    Уже имеющиеся \\n обрабатываются независимо.
    """
    if not _HAS_CYRILLIC.search(text):
        return text

    if '\\n' in text:
        return '\\n'.join(wrap_text(part, limit) for part in text.split('\\n'))

    words = text.split(' ')
    out_lines = []
    current = ''

    for word in words:
        if not word:
            continue

        sep = '' if current == '' else ' '
        candidate = current + sep + word

        if len(candidate) <= limit:
            current = candidate
            continue

        space_left = limit - len(current) - (1 if current else 0)
        positions  = _syllable_positions(word)

        best_here = None
        for p in positions:
            if p + 1 <= space_left and p >= 2:
                best_here = p

        if best_here:
            out_lines.append(current + sep + word[:best_here] + WRAP_HYPHEN)
            rest = word[best_here:]
            if len(rest) > limit:
                current = _split_long_word(rest, limit, out_lines)
            else:
                current = rest
        else:
            if current:
                out_lines.append(current)
            if len(word) <= limit:
                current = word
            else:
                current = _split_long_word(word, limit, out_lines)

    if current:
        out_lines.append(current)

    return '\\n'.join(out_lines)


# ── dump-xlsx ────────────────────────────────────────────────────────────────

def cmd_dump_xlsx(args):
    try:
        import openpyxl
        from openpyxl.styles import Font, PatternFill, Alignment
    except ImportError:
        print("Установите openpyxl:  pip install openpyxl")
        return

    if len(args) < 2:
        print("Использование: utfdump.py dump-xlsx <папка|file.utf> <output.xlsx>")
        return

    src = Path(args[0])
    out = Path(args[1])

    files = []
    if src.is_dir():
        for f in sorted(src.glob('*.utf')):
            files.append(f)
    elif src.suffix.lower() == '.utf':
        files.append(src)
    else:
        print(f"Неизвестный источник: {src}")
        return

    if not files:
        print("Файлы .utf не найдены.")
        return

    wb = openpyxl.Workbook()
    wb.remove(wb.active)

    hdr_font  = Font(bold=True, color='FFFFFF')
    hdr_fill  = PatternFill('solid', fgColor='2F4F8F')
    orig_fill = PatternFill('solid', fgColor='F0F0F0')
    tlc_fill  = PatternFill('solid', fgColor='EAF4EA')
    center    = Alignment(horizontal='center', vertical='top')
    wrap_al   = Alignment(wrap_text=True, vertical='top')

    total = 0
    for utf_path in files:
        lines = read_utf_lines(utf_path)
        rows  = get_translatable_rows(lines)

        if not rows:
            print(f"  {utf_path.name}: нет строк для перевода, пропускаем")
            continue

        sheet_name = utf_path.stem[:31]
        ws = wb.create_sheet(title=sheet_name)

        headers = ['Row', 'Original JP', 'TL (RU)', 'TLC']
        for col, h in enumerate(headers, 1):
            cell = ws.cell(row=1, column=col, value=h)
            cell.font      = hdr_font
            cell.fill      = hdr_fill
            cell.alignment = center

        ws.column_dimensions['A'].width = 12
        ws.column_dimensions['B'].width = 55
        ws.column_dimensions['C'].width = 55
        ws.column_dimensions['D'].width = 55
        ws.row_dimensions[1].height = 20

        for xls_row, s in enumerate(rows, 2):
            ws.cell(row=xls_row, column=1, value=s['row_id']).alignment = center
            c_orig = ws.cell(row=xls_row, column=2, value=s['text'])
            c_orig.fill      = orig_fill
            c_orig.alignment = wrap_al
            tl_cell = ws.cell(row=xls_row, column=3, value='')
            tl_cell.alignment = wrap_al
            tlc_cell = ws.cell(row=xls_row, column=4, value='')
            tlc_cell.fill      = tlc_fill
            tlc_cell.alignment = wrap_al

        ws.auto_filter.ref = f"A1:D{len(rows)+1}"
        ws.freeze_panes    = 'A2'

        total += len(rows)
        print(f"  {utf_path.name}: {len(rows)} строк")

    wb.save(out)
    print(f"\nСохранено: {out}  ({total} строк, {len(files)} листов)")


# ── wrap ─────────────────────────────────────────────────────────────────────

def cmd_wrap(args):
    try:
        import openpyxl
        from openpyxl.styles import Alignment
    except ImportError:
        print("Установите openpyxl:  pip install openpyxl")
        return

    if len(args) < 2:
        print("Использование: utfdump.py wrap <input.xlsx> <output.xlsx>")
        return

    in_path  = Path(args[0])
    out_path = Path(args[1])

    if not in_path.exists():
        print(f"Файл не найден: {in_path}")
        return

    _get_hyph()

    wb = openpyxl.load_workbook(in_path)
    wrap_al = Alignment(wrap_text=True, vertical='top')

    total_wrapped = 0
    total_cells   = 0

    for sheet_name in wb.sheetnames:
        if sheet_name.upper() in ('SEEN0100',):
            continue
        ws = wb[sheet_name]
        sheet_wrapped = 0

        for row in ws.iter_rows(min_row=2):
            # Обрабатываем колонки C (TL) и D (TLC)
            for col_idx in (2, 3):   # 0-based: col C=2, col D=3
                if col_idx >= len(row):
                    continue
                cell = row[col_idx]
                val = cell.value
                if not val or not str(val).strip():
                    continue

                text = str(val).strip()
                total_cells += 1

                wrapped = wrap_text(text)
                if wrapped != text:
                    cell.value     = wrapped
                    cell.alignment = wrap_al
                    sheet_wrapped += 1

        if sheet_wrapped:
            print(f"  {sheet_name}: перенесено {sheet_wrapped} ячеек")
        total_wrapped += sheet_wrapped

    wb.save(out_path)
    print(f"\nГотово: {total_wrapped} ячеек из {total_cells} переработаны -> {out_path}")


# ── Словарь имён персонажей ──────────────────────────────────────────────────

_NAMES_MAP = None

def get_names_map():
    global _NAMES_MAP
    if _NAMES_MAP is not None:
        return _NAMES_MAP
    names_file = Path(__file__).parent / 'names.json'
    if names_file.exists():
        try:
            import json
            _NAMES_MAP = json.loads(names_file.read_text(encoding='utf-8'))
        except Exception as e:
            print(f"Ошибка загрузки names.json: {e}")
            _NAMES_MAP = {}
    else:
        _NAMES_MAP = {}
    return _NAMES_MAP

def translate_speaker_raw(name: str) -> str:
    """Перевести имя персонажа (японское -> русское)."""
    m = get_names_map()
    name = name.strip()
    if name in m:
        return m[name]
    if '／' in name or '/' in name:
        delim = '／' if '／' in name else '/'
        parts = [p.strip() for p in name.split(delim)]
        return ' / '.join(m.get(p, p) for p in parts)
    if '＆' in name or '&' in name:
        delim = '＆' if '＆' in name else '&'
        parts = [p.strip() for p in name.split(delim)]
        return ' и '.join(m.get(p, p) for p in parts)
    return name

def replace_speaker_tags(text: str) -> str:
    """
    Заменяет японские имена внутри \\{...} и {...} на русские.
    Не трогает теги управления (\\ruby, \\size, \\em и т.д.).
    """
    m = get_names_map()
    if not m:
        return text

    def _replace_match(match):
        prefix = match.group(1)  # "\\" или ""
        inside = match.group(2).strip()
        if any(inside.startswith(p) for p in ('ruby', 'size', 'em', 'color', 'wait', 'speed', 'res', 'd', 'f', 'g', 'ident', 'voice')):
            return match.group(0)
        ru = translate_speaker_raw(inside)
        if ru != inside:
            return f"{prefix}{{{ru}}}"
        return match.group(0)

    return re.sub(r'(\\?)\{([^}]+)\}', _replace_match, text)


# ── from-xlsx ────────────────────────────────────────────────────────────────

def cmd_from_xlsx(args):
    try:
        import openpyxl
    except ImportError:
        print("Установите openpyxl:  pip install openpyxl")
        return

    if len(args) < 3:
        print("Использование: utfdump.py from-xlsx <папка_с_utf/> <input.xlsx> <папка_вывода/>")
        return

    utf_dir   = Path(args[0])
    xlsx_path = Path(args[1])
    out_dir   = Path(args[2])

    if not utf_dir.is_dir():
        print(f"Папка с .utf не найдена: {utf_dir}")
        return
    if not xlsx_path.exists():
        print(f"Файл xlsx не найден: {xlsx_path}")
        return

    out_dir.mkdir(parents=True, exist_ok=True)
    _get_hyph()
    get_names_map()

    wb = openpyxl.load_workbook(xlsx_path, data_only=True)

    total_files   = 0
    total_applied = 0

    for sheet_name in wb.sheetnames:
        ws = wb[sheet_name]

        utf_path = utf_dir / f"{sheet_name}.utf"
        if not utf_path.exists():
            print(f"  {sheet_name}.utf: файл не найден в {utf_dir}, пропускаем")
            continue

        lines = read_utf_lines(utf_path)
        rows  = get_translatable_rows(lines)

        # Собираем переводы из xlsx: список (row_id, tl, tlc) в порядке строк
        xlsx_rows = []
        for xls_row in ws.iter_rows(min_row=2, values_only=True):
            if not xls_row or xls_row[0] is None:
                continue
            row_id = str(xls_row[0]).strip()
            tl     = str(xls_row[2]).strip() if xls_row[2] else ''
            tlc    = str(xls_row[3]).strip() if len(xls_row) > 3 and xls_row[3] else ''
            xlsx_rows.append((row_id, tl, tlc))

        new_lines = list(lines)
        applied   = 0

        n = min(len(rows), len(xlsx_rows))
        for i in range(n):
            file_row  = rows[i]
            xls_row_id, tl, tlc = xlsx_rows[i]
            row_id = file_row['row_id']

            # Выбираем текст перевода: TLC > TL > оригинал
            chosen = tlc if tlc else (tl if tl else '')
            if not chosen:
                # Если перевода в Excel нет, но это #character — переводим имя через names.json
                if row_id == '#character':
                    raw_name = file_row['text'].strip("'\"")
                    ru_name = translate_speaker_raw(raw_name)
                    if ru_name and ru_name != raw_name:
                        ru_f_name = apply_ru_f(ru_name)
                        new_lines[file_row['line_index']] = f"#character '{ru_f_name}'"
                        applied += 1
                continue

            # Для строки <0000> (заголовок окна/сцены) удаляем служебные слэши и японские пробелы в начале
            if row_id == '<0000>':
                chosen = chosen.lstrip('\\ \t\u3000')

            # Заменяем японские имена персонажей в репликах \{Имя} -> \{РусскоеИмя}
            chosen = replace_speaker_tags(chosen)

            # Применяем перенос и RU_F кодировку (для меню выбора вроде SEEN0100 не переносим)
            if sheet_name.upper() in ('SEEN0100',):
                chosen_wrapped = chosen.replace('\n', ' ').replace('\r', '')
            else:
                chosen_wrapped = wrap_text(chosen)
            chosen_encoded  = apply_ru_f(chosen_wrapped)

            # Восстанавливаем строку с её тегом
            if row_id == '#character':
                clean_enc = chosen_encoded.strip("'\"")
                new_line = f"#character '{clean_enc}'"
            else:
                new_line = f"{row_id} {chosen_encoded}"

            new_lines[file_row['line_index']] = new_line
            applied += 1

        # Дополнительно: проверяем директивы #character в начале файла, если они не попали под замену
        for idx, line in enumerate(new_lines):
            m_ch = _CHAR_RE.match(line)
            if m_ch:
                raw_name = m_ch.group(1).strip("'\"")
                ru_name = translate_speaker_raw(raw_name)
                if ru_name and ru_name != raw_name:
                    ru_f_name = apply_ru_f(ru_name)
                    new_lines[idx] = f"#character '{ru_f_name}'"

        out_path = out_dir / f"{sheet_name}.utf"
        out_path.write_text('\n'.join(new_lines) + '\n', encoding='utf-8')

        total_files   += 1
        total_applied += applied
        print(f"  {sheet_name}.utf: применено {applied} переводов -> {out_path}")

    print(f"\nГотово: {total_files} файлов, {total_applied} строк переведено -> {out_dir}")


# ── main ─────────────────────────────────────────────────────────────────────

def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        sys.exit(1)

    cmd = args[0].lower()

    if cmd == 'dump-xlsx':
        cmd_dump_xlsx(args[1:])
    elif cmd == 'wrap':
        cmd_wrap(args[1:])
    elif cmd == 'from-xlsx':
        cmd_from_xlsx(args[1:])
    else:
        print(__doc__)
        sys.exit(1)


if __name__ == '__main__':
    main()
