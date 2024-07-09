/*** includes ***/

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/
#define KILO_VERSION "0.0.1"

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

struct editorConfig { // структура конфигурации
    int cx, cy; // позиция курсора
    int screenrows;
    int screencols;
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

void editorDrawRows(struct abuf *ab) {
    int y;
    for (y = 0; y < E.screenrows; y++) {
        if (y == E.screenrows / 3) {    // Если Текущая строка равна трети высоты экрана
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
        
        abAppend(ab, "\x1b[K", 3);    // очистить строку. K (Стереть в строке) - стирает часть текущей строки
        if (y < E.screenrows - 1) { // если строка не последняя вывести \r\n
            abAppend(ab, "\r\n", 2);    // перевод курсора в начало строки
        }
    }
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
    struct abuf ab = ABUF_INIT;

    abAppend(&ab, "\x1b[?25l", 6);   // скрыть курсор 
    abAppend(&ab, "\x1b[H", 3);     // перевести курсор в начало экрана

    editorDrawRows(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);  // Позиция курсора
    abAppend(&ab, buf, strlen(buf));    // перевести курсор в начало экрана

    abAppend(&ab, "\x1b[?25h", 6);  // показать курсор

    write(STDOUT_FILENO, ab.b, ab.len); //  запись в терминал
    abFree(&ab);
}

/*** input ***/

void editorMoveCursor(int key) {
    switch (key) {
        case ARROW_LEFT:
            if (E.cx != 0) {
                E.cx--;
            }
            break;
        case ARROW_RIGHT:
            if (E.cx != E.screencols -1 ) {
                E.cx++;
            }
            break;
        case ARROW_UP:
            if (E.cy != 0) {
                E.cy--;
            }
            break;
        case ARROW_DOWN:
            if (E.cy != E.screenrows - 1) {
                E.cy++;
            }
            break;
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
            E.cx = E.screencols - 1;
            break;

        case PAGE_UP:
        case PAGE_DOWN:
            {   /* Прокрутка экрана вниз и вверх */
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

    if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}

int main() {
    enableRawMode();
    initEditor();

    while (1) {
        editorRefreshScreen();
        editorProcessKeyPress();
    }
        
    return 0;
}