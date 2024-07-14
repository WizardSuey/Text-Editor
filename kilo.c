/*** includes ***/

/*
    * Следующие макросы используются для включения определенных функций C и POSIX.
    * библиотеки.
    *
    * _DEFAULT_SOURCE: этот макрос сообщает компилятору включить функции из
    * Стандартная библиотека C с объявлениями по умолчанию.
    *
    * _BSD_SOURCE: Этот макрос сообщает компилятору включить функции из
    * Библиотека BSD с объявлениями по умолчанию.
    *
    * _GNU_SOURCE: Этот макрос сообщает компилятору включить функции из
    * Библиотека GNU C с объявлениями по умолчанию.
    *
    * Целью этих макросов является включение функций, которые
    * не включено по умолчанию при компиляции кода C.
    *
    * Например, макрос _BSD_SOURCE используется для включения функций из
    * Библиотека BSD, такая как strlcpy() и strlcat().
    *
    * Макрос _GNU_SOURCE используется для включения функций, специфичных для
    * Библиотека GNU C, например basename() и dirname().
    *
    * Включив эти макросы в начало исходного файла, компилятор
    * сможете найти объявления этих функций и включить их
    * в скомпилированном коде.
 */

#define _DEFAULT_SOURCE 
#define _BSD_SOURCE     
#define _GNU_SOURCE      

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/*** defines ***/
#define KILO_VERSION "0.0.1"
#define KILO_TAB_STOP 8

#define CTRL_KEY(k) ((k) & 0x1f) // применение маски 00011111 к коду клавиши

enum editorKey {    
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN
};

/*** data ***/

typedef struct erow {   // тип данных для строки
    int size;
    int rsize;  // размер render
    char *chars; // символы строки                                                                
    char *render;   // содержит фактические символы, которые нужно рисовать на экране
    /*
        Пример:
        chars = "\tvar foo = 123\n\0";
        render = "        var foo = 123";

    */
} erow;

struct editorConfig { // структура конфигурации
    int cx, cy; // позиция курсора в chars
    int rx; // позиция курсора в render
    int coloff; // смещение столбца, перед которым расположен курсор
    int rowoff; // смещение строки, перед которым расположен курсор
    int screenrows;
    int screencols;
    int numrows;
    erow *row;
    char *filename; // имя файла
    char statusmsg[80];
    time_t statusmsg_time;
    struct termios orig_termios;    // исходные атрибуты терминала
};

struct editorConfig E;


/*** terminal ***/

void die(const char *s) {
    write(STDOUT_FILENO, "\x1b[2J", 4); // очистка всего экрана
    write(STDOUT_FILENO, "\x1b[H", 3); // перевод курсора в начало экрана
    perror(s);
    exit(1);
}

void disableRawMode() {
    /* Отключает необработанный режим, восстанавливая исходные значения атрибутов терминала */
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) {
        die("tcsetattr");
    }  
}
void enableRawMode() {
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr"); // Прочитать атрибуты ввода в переменную raw
    atexit(disableRawMode); // вызов функции disableRawMode при выходе из программы
    
    struct termios raw = E.orig_termios; // Копировать атрибуты ввода в raw
    // Задаем атрибуты для чтения символов
    // BRKINT - прерывание при появлении ошибки остановки ввода
    // ICRNL - преобразование символа возврата каретки в символ переноса строки
    // INPCK - проверка четности битов на входе
    // ISTRIP - удаление восьмой битовой битовой маски
    // IXON - включение управления потоком данных
    // Всё это отключаем, чтобы ввод был более совершен
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON); 

    // Задаем атрибуты для настройки скорости символов
    // CS8 - 8-битовая маска, которую устанавливаем и устанавливаем размер символов равным 8 бит на байт 
    raw.c_cflag |= (CS8); 

    raw.c_oflag &= ~(OPOST); // \n и \r отключены  
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG); // Отключить эхо и канонический режим и сигналы CTRL+C и CTRL+Z, CTRL+V
    raw.c_cc[VMIN] = 0; // минимальное количество входных байтов, необходимое для возврата функции read()
    raw.c_cc[VTIME] = 1; // устанавливает максимальное время ожидания перед возвратом функции read()

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr"); // Установить атрибуты ввода
}

int editorReadKey() {
    /* дождаться одного нажатия клавиши и вернуть его */
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) { // Чтение одного символа
        if (nread == -1 && errno != EAGAIN) die("read"); // Если не удалось прочитать символ, вывести ошибку и выйти
    }

    if (c == '\x1b') {
        char seq[3];    // Буфер для esc-последовательности

        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

        if (seq[0] == '[') {    // Если esc-последовательность
            if (seq[1] >= '0' && seq[1] <= '9') {   // Если esc-последовательность начинается с [0-9]
                if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                if (seq[2] == '~') {    // ЕСли esc-последовательность заканчивается на ~
                    switch (seq[1]) {
                        case '1': return HOME_KEY;
                        case '3': return DEL_KEY;
                        case '4': return END_KEY;
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                        case '7': return HOME_KEY;
                        case '8': return END_KEY;
                    }
                }
            } else {
                switch (seq[1]) {
                    case 'A': return ARROW_UP;
                    case 'B': return ARROW_DOWN;
                    case 'C': return ARROW_RIGHT;
                    case 'D': return ARROW_LEFT;
                    case 'H': return HOME_KEY;
                    case 'F': return END_KEY;
                }
            }
        } else if (seq[0] == '0') { // если esc-последовательность начинается с 0
            switch (seq[1]) {
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
            }
        }

        return '\x1b';
    } else {
        return c;
    }
}

int getCursorPosition(int *rows, int *cols) {
    /*  возвращает текущую позицию курсора в терминале 
    
        esc-последовательность:
        n (Отчет о состоянии устройства) - может использоваться для запроса терминалом информации о состоянии
        аргумент 6, чтобы запросить позицию курсора 
    */

    char buf[32]; // Буфер для хранения информации о позиции курсора
    unsigned int i = 0;
    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

    /* продолжать читать символы, пока не доберемся до символа R */
    while (i < sizeof(buf) - 1) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1) break; // Чтение одного символа 
        if (buf[i] == 'R') break;
        i++;
    }
    buf[i] = '\0';

    if (buf[0] != '\x1b' || buf[1] != '[') return -1; // Если ответ НЕ esc-последовательности вернуть -1
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1; // Если не удалось распознать два числа, вернуть -1

    return 0;
}

int getWindowSize(int *rows, int *cols) {
    /* 
        В случае успеха ioctl() поместит количество столбцов по ширине и количество строк по высоте, 
        в которых находится терминал, в данную структуру winsize

        TIOCGWINSZ - возвращает размер окна терминала

        esc-последовательность:
        С  (курсор вперед) - перемещает курсор вправо
        B (Курсор вниз) - перемещает курсор вниз
        999 - перемещает курсор вниз/вправо на 999 строк
    */
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) { // 
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) die("write"); // если не удалось переместить курсор, вывести ошибку и выйти
        return getCursorPosition(rows, cols);
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
} 

/*** row operations ***/

int editorRowCxToRx(erow *row, int cx) {
    /*
        Для каждого символа, если это табуляция, мы используем rx % KILO_TAB_STOP, чтобы узнать, 
        сколько табов находится слева от последней позиции табуляции, 
        а затем вычитаем это из KILO_TAB_STOP - 1, чтобы узнать, 
        сколько столбцов находимся справа от следующая позиция табуляции. 
        Добавляем эту сумму к rx, чтобы оказаться справа от следующей позиции табуляции, 
        а затем rx++ переводит нас прямо на следующую позицию табуляции.
    */
    int rx = 0;
    int j;
    for (j = 0; j < cx; j++) {
        if (row->chars[j] == '\t')
            rx += (KILO_TAB_STOP - 1) - (rx % KILO_TAB_STOP);
        rx++;
    }
    return rx;
}

void editorUpdateRow(erow *row) {
    /* 
        использует строку символов строки для заполнения содержимого строки render. 
        Скопируем каждый символ из chars в render
     */
    int tabs = 0;
    int j;
    /* 
        Во-первых, нам нужно перебрать символы строки и посчитать табуляции, 
        чтобы узнать, сколько памяти выделить для рендеринга
    */ //  Максимальное количество символов, необходимое для каждого таба, равно 8
    /*
        row->size уже учитывает 1 для каждого таба, 
        поэтому мы умножаем количество табов на 7 и добавляем это к rowsize, 
        чтобы получить максимальный объем памяти, 
        который нам понадобится для рендеримой строки.
    */
    for (j = 0; j < row->size; j++) 
        if (row->chars[j] == '\t') tabs++;

    free(row->render);
    row->render = malloc(row->size + tabs*(KILO_TAB_STOP - 1) + 1);   //  создание буфера для рендеринга строки с табуляциями, которые равны 8 символам каждая

    int idx = 0;
    /*
        Является ли текущий символ табуляцией. 
        Если это так, мы добавляем один пробел (потому что каждая табуляция должна передвигать курсор вперед хотя бы на один столбец), 
        а затем добавляем пробелы, пока не доберемся до позиции табуляции, 
        которая представляет собой столбец, который делится на 8.
    */
    for (j = 0; j < row->size; j++) {
        if (row->chars[j] == '\t') {
            row->render[idx++] = ' ';
            while (idx % KILO_TAB_STOP != 0) row->render[idx++] = ' ';
        } else {
            row->render[idx++] = row->chars[j];
        }
    }
    row->render[idx] = '\0';
    row->rsize = idx;
}

void editorAppendRow(char *s, size_t len) {
    /* Добавляет новую строку в буфер редактора. */
    E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1)); // выделение памяти под новую строку

    int at = E.numrows;
    E.row[at].size = len;
    E.row[at].chars = malloc(len + 2);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';
    E.numrows++; 

    E.row[at].rsize = 0;
    E.row[at].render = NULL;
    editorUpdateRow(&E.row[at]);
}         

/*** file i/o ***/

void editorOpen(char *filename) {
    /* Открывает файл, указанный в параметре filename, 
        и считывает содержимое построчно и заполняет структуру строк редактора содержимым. */
    free(E.filename);

    /* создает копию заданной строки, выделяя необходимую память и предполагая, что мы free() эту память. */
    E.filename = strdup(filename);  //  копируем имя файла

    FILE *fp = fopen(filename, "r");
    if (!fp) die("fopen");

    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    while ((linelen = getline(&line, &linecap, fp)) != -1) {    // Пока не конец файла
        while (linelen > 0 && (line[linelen - 1] == '\n' || // если последний символ \n или \r
                                line[linelen - 1] == '\r')) 
            linelen--;
        editorAppendRow(line, linelen);
        
    }
    free(line);
    fclose(fp);
}

/*** append buffer ***/

struct abuf {   // Буфер для текста в терминале
    char *b;    // указатель на наш буфер в памяти
    int len;    // Длина буфера
};

#define ABUF_INIT {NULL, 0} // пустой буфер

void abAppend(struct abuf *ab, const char *s, int len) {
    /* Добавить строку к буферу */
    char *new = realloc(ab->b, ab->len + len); // расширить буфер в памяти

    if (new == NULL) return;
    /*
        Копировать строку (s) в конец буфера (new) с размером (len).
        new - указатель на начало буфера
        ab->len - указатель на текущую длину буфера
        s - указатель на начало строки
        len - размер строки
    */
    memcpy(&new[ab->len], s, len); 
    ab->b = new;    // указатель на новый буфер
    ab->len += len; // увеличить длину буфера
}

void abFree(struct abuf *ab) {
    /* Очистить буфер */
    free(ab->b);
}

/*** output ***/

void editorScroll() {
    /* Прокручивает экран, если курсор вышел за границы экрана. */
    E.rx = 0;
    if (E.cy < E.numrows) {
        E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
    }
    if (E.cy < E.rowoff) {
        /* проверяет, находится ли курсор над видимым окном, и если да,
         то прокручивает его до того места, где находится курсор.*/
        E.rowoff = E.cy;
    }
    if (E.cy >= E.rowoff + E.screenrows) {
        /* проверяет, находится ли курсор за нижней частью видимого окна, 
        и содержит немного более сложную арифметику, поскольку E.rowoff ссылается на то, 
        что находится в верхней части экрана, 
        и нам нужно задействовать E.screenrows, 
        чтобы говорить о том, что находится в нижней части экрана.*/
        E.rowoff = E.cy - E.screenrows + 1;
    }
    if (E.rx < E.coloff) {
        E.coloff = E.rx;
    }
    if (E.rx >= E.coloff + E.screencols) {
        E.coloff = E.rx - E.screencols + 1;
    }
}

void editorDrawRows(struct abuf *ab) {
    int y;
    for (y = 0; y < E.screenrows; y++) {
        int filerow = y + E.rowoff; // номер строки в текстовом буфере
        if (filerow >= E.numrows) {   //  если номер строки больше или равен кол-ва строк в текстовом буфере
            if (E.numrows == 0 && y == E.screenrows / 3) {    // Если Строк 0 и кол-во скрок равны трети высоты экрана
                char welcome[80];   // буфер для приветствия
                int welcomelen = snprintf(welcome, sizeof(welcome), 
                    "Kilo editor -- version %s", KILO_VERSION); // приветствие

                if (welcomelen > E.screencols) welcomelen = E.screencols;    //  если приветствие больше чем ширина экрана то длина приветствия равна ширине экрана
                int padding = (E.screencols - welcomelen) / 2;  // отступ от центра экрана
                if (padding) {  // если отступ больше нуля
                    abAppend(ab, "~", 1);
                    padding--;
                }
                while (padding--) abAppend(ab, " ", 1); // Пока отступ больше нуля добавить пробел

                abAppend(ab, welcome, welcomelen); // добавить приветствие
            } else {
                abAppend(ab, "~", 1); // заполнить буфер символом ~
            }
        } else {
            int len = E.row[filerow].rsize - E.coloff; // длина строки в текстовом буфере с отступом от курсора
            if (len < 0) len = 0;
            if (len > E.screencols) len = E.screencols; // если длина строки больше ширины экрана то длина строки равна ширине экрана
            abAppend(ab, &E.row[filerow].render[E.coloff], len); //  добавить строку к буферу
        }
        
        abAppend(ab, "\x1b[K", 3);    // очистить строку. K (Стереть в строке) - стирает часть текущей строки
        abAppend(ab, "\r\n", 2);    // перевод курсора в начало строки
        }
}

void editorDrawStatusBar(struct abuf *ab) {
    /* Рисует строку состояния в нижней части экрана инвертированными цветами. */

    /*
        m (Select Graphic Rendition) - приводит к печати текста, 
        напечатанного после него, с различными возможными атрибутами, 
        включая жирный шрифт (1), подчеркивание (4), мигание (5) и инвертированные цвета (7). Н-р \1xb[1;4;5;7m
    */
    abAppend(ab, "\x1b[7m", 4); //  переключает цвета на инвертированные
    char status[80], rstatus[80];   // буферы для названия и общим кол-во срок И правой части строки состояния с количеством строк и текущей строке
    int len = snprintf(status, sizeof(status), "%.20s - %d lines", E.filename ? E.filename : "[No Name]", E.numrows);   // строка состояния левая
    int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d", E.cy + 1, E.numrows); // Правая часть строки состояния с количеством строк и текущей строки
    if (len > E.screencols) len = E.screencols;
    abAppend(ab, status, len);

    while (len < E.screencols) {
        if (E.screencols - len == rlen) {   // если 
            abAppend(ab, rstatus, rlen);
            break;
        } else {
            abAppend(ab, " ", 1);
            len++;
        }
    }
    abAppend(ab, "\x1b[m", 3); // переключает обратно на нормальное форматирование
    abAppend(ab, "\r\n", 2);
}

void editorDrawMessageBar(struct abuf *ab) {
    abAppend(ab, "\x1b[K", 3);  // очистить строку. K (Стереть в строке) - стирает часть текущей строки
    int msglen = strlen(E.statusmsg);
    if (msglen > E.screencols) msglen = E.screencols;
    if (msglen && time(NULL) - E.statusmsg_time < 5)    // если время выполнения команды меньше 5 секунд то вывести сообщение
        abAppend(ab, E.statusmsg, msglen);
}

void editorRefreshScreen() {
    /*  Запись в терминал 4 байта 
        \x1b - 1 байт escape-символ или 27 в десятичном формате
        [2j - 3 байта 

        Любая escape-команда начинается с \x1b[
        2J - очистка всего экрана
        H (Позиция курсора) - перевод курсора в начало экрана
        h (Set Mode), l (Reset Mode) - включение и выключение различных функций или «режимов» терминала
    */

    editorScroll();

    struct abuf ab = ABUF_INIT;

    abAppend(&ab, "\x1b[?25l", 6);   // скрыть курсор 
    abAppend(&ab, "\x1b[H", 3);     // перевести курсор в начало экрана

    editorDrawRows(&ab);
    editorDrawStatusBar(&ab);
    editorDrawMessageBar(&ab);

    char buf[32];
    /* Форматирует строку с escape-последовательностью для перемещения курсора в позицию (E.cy + 1, E.rx + 1) 
       В эту строку вставляются целые числа E.cy (строка) и E.rx (столбец), 
       но поскольку escape-последовательности требуют отсчет от 1, а не от 0, 
       мы прибавляем к ним 1. 
       Также, поскольку курсор в escape-последовательности отсчитывается с начала экрана, а не с начала строки, 
       мы вычитаем из каждого числа E.rowoff и E.coloff, чтобы вычислить смещение курсора относительно начала экрана.
    */
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", /* escape-последовательность для перемещения курсора в позиции (строка, столбец) */
        (E.cy - E.rowoff) + 1, /* строка */
        (E.rx - E.coloff) + 1 /* столбец */
    );
    abAppend(&ab, buf, strlen(buf));    // перевести курсор в начало экрана

    abAppend(&ab, "\x1b[?25h", 6);  // показать курсор

    write(STDOUT_FILENO, ab.b, ab.len); //  запись в терминал
    abFree(&ab);
}

void editorSetStatusMessage(const char *fmt, ...) {
    /* 
        Устанавливает сообщение о состоянии, 
        форматируя ввод с использованием списка переменных аргументов и 
        сохраняя его в E.statusmsg вместе с текущим временем в E.statusmsg_time. 
    */
    va_list ap; // список аргументов
    va_start(ap, fmt);  // инициализация списка аргументов
    vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);   // печатает форматированный список аргументов в E.statusmsg
    va_end(ap);
    E.statusmsg_time = time(NULL);
}

/*** input ***/

void editorMoveCursor(int key) {
    erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy]; // если строка не существует то erow = NULL, иначе переменная строки будет указывать на строку, на которой находится курсор
    switch (key) {
        case ARROW_LEFT:
            if (E.cx != 0) {
                E.cx--;
            } else if (E.cy > 0) {  // 
                E.cy--;
                E.cx = E.row[E.cy].size;
            }
            break;
        case ARROW_RIGHT:
            if (row && E.cx < row->size) {  // если строка существует и курсор не в конце строки,
                E.cx++;
            } else if (row && E.cx == row->size) {  //
                E.cy++;
                E.cx = 0;
            }
            break;
        case ARROW_UP:
            if (E.cy != 0) {
                E.cy--;
            }
            break;
        case ARROW_DOWN:
            if (E.cy < E.numrows) {
                E.cy++;
            }
            break;
    }

    /* 
    Нам нужно снова установить строку, поскольку E.cy может указывать на другую строку, чем раньше. 
    Затем мы устанавливаем E.cx в конец этой строки, если E.cx находится справа от конца этой строки. 
    */
    row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    int rowlen = row ? row->size : 0;
    if (E.cx > rowlen) {
        E.cx = rowlen;
    }
}

void editorProcessKeyPress() {
    /* ожидает нажатия клавиши, а затем обрабатывает его */
    int c = editorReadKey(); 

    switch (c) { // Обрабатывает нажатие клавиши
        case CTRL_KEY('q'): // Завершение программы при нажатии CTRL+Q
            write(STDOUT_FILENO, "\x1b[2J", 4); // очистка всего экрана
            write(STDOUT_FILENO, "\x1b[H", 3); // перевод курсора в начало экрана
            exit(0);
            break;
        
        case HOME_KEY:
            E.cx = 0;
            break;

        case END_KEY:
        if (E.cy < E.numrows)   // если строка существует
                E.cx = E.row[E.cy].size;
            break;

        case PAGE_UP:
        case PAGE_DOWN:
            {   /* Прокрутка экрана вниз и вверх */
                if (c == PAGE_UP) {
                    E.cy = E.rowoff;
                } else if (c == PAGE_DOWN) {
                    E.cy = E.rowoff + E.screenrows - 1;
                    if (E.cy > E.numrows) E.cy = E.numrows;
                }

                int times = E.screenrows;
                while (times--)
                    editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
            }
            break;

        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            editorMoveCursor(c);
            break;
    }
}

/*** init ***/

void initEditor() {
    /* Инициализация  всех полей в структуре E */
    E.cx = 0;
    E.cy = 0;
    E.rx = 0;
    E.rowoff = 0;
    E.coloff = 0;
    E.numrows = 0;
    E.row = NULL;
    E.filename = NULL;
    E.statusmsg[0] = '\0';
    E.statusmsg_time = 0;

    if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
    E.screenrows -= 2;
}

int main(int argc, char *argv[]) {
    enableRawMode();
    initEditor();

    if (argc >= 2) {
        editorOpen(argv[1]);
    }

    editorSetStatusMessage("HELP: CTRL-Q = quit");

    while (1) {
        editorRefreshScreen();
        editorProcessKeyPress();
    }
        
    return 0;
}