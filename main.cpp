#include <iostream>
#include <vector>
#include <fstream>
#include <sstream>
#include <string>
#include <algorithm>
#include <cstdlib>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <iomanip>

/* --- Linux Terminal Config --- */
struct termios orig_termios;

void disableRawMode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enableRawMode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disableRawMode);

    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
    
    raw.c_iflag &= ~(IXON | ICRNL);
    
    raw.c_oflag &= ~(OPOST);
    
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}
/* ----------------------------- */

#define CTRL_KEY(k) ((k) & 0x1f)

int get_window_size(int *rows, int *cols) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        return -1;
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

enum EditorKey {
  BACKSPACE = 127,
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN
};

struct Line {
    std::string contents;
    Line(std::string s = "") : contents(s) {}
};

struct Editor {
    int cursor_x;
    int cursor_y;
    int row_offset; // Scroll pionowy
    int col_offset; // Scroll poziomy (nowość!)
    int screen_rows;
    int screen_cols;
    std::vector<Line> lines;
    std::string file_name;
    
    Editor(const std::string &file_name){
        this->file_name = file_name;
        this->cursor_x = 0;
        this->cursor_y = 0;
        this->row_offset = 0;
        this->col_offset = 0;
        
        if (get_window_size(&screen_rows, &screen_cols) == -1) {
            screen_rows = 24;
            screen_cols = 80;
        }
    }
};

void open_file(Editor &editor){
    std::ifstream file(editor.file_name);
    
    editor.lines.clear();

    if(file.is_open()) {
        std::string line_str;
        // std::getline automatycznie obsługuje wczytywanie linii o dowolnej długości
        while(std::getline(file, line_str)) {
            // Usuń ewentualny znak \r na końcu (dla plików z Windowsa)
            if (!line_str.empty() && line_str.back() == '\r') {
                line_str.pop_back();
            }
            editor.lines.push_back(Line(line_str));
        }
        file.close();
    }

    // Jeśli plik był pusty, dodaj jedną pustą linię
    if (editor.lines.empty()) {
        editor.lines.push_back(Line(""));
    }
}

void save_buffer(const Editor &editor){
    std::ofstream file(editor.file_name);
    if(file.is_open()){
        for(const auto &line : editor.lines){
            file << line.contents << "\n";
        }
        file.close();
    }
}

// Logika scrollowania (pionowo i poziomo)
void editor_scroll(Editor &editor) {
    // --- PIONOWO ---
    if (editor.cursor_y < editor.row_offset) {
        editor.row_offset = editor.cursor_y;
    }
    if (editor.cursor_y >= editor.row_offset + editor.screen_rows) {
        editor.row_offset = editor.cursor_y - editor.screen_rows + 1;
    }

    // --- POZIOMO ---
    // Offset na numery linii (zmienna wartość w print_buffer, tutaj przyjmijmy stałą korektę logiczną)
    // W tym prostym modelu scrollujemy treść.
    if (editor.cursor_x < editor.col_offset) {
        editor.col_offset = editor.cursor_x;
    }
    // Odejmujemy margines na numery linii (np. 6 znaków) od szerokości ekranu
    int available_cols = editor.screen_cols - 6; 
    if (editor.cursor_x >= editor.col_offset + available_cols) {
        editor.col_offset = editor.cursor_x - available_cols + 1;
    }
}

void print_buffer(Editor &editor){
    editor_scroll(editor);

    std::cout << "\x1b[?25l"; // Ukryj kursor
    std::cout << "\x1b[H";    // Home

    for(int y = 0; y < editor.screen_rows; y++){
        int file_row = y + editor.row_offset;
        
        std::cout << "\x1b[K"; // Wyczyść linię

        if (file_row < (int)editor.lines.size()) {
            // Wyświetl numer linii
            std::cout << std::setw(4) << (file_row + 1) << "  ";
            
            // Pobierz linię
            std::string &row = editor.lines[file_row].contents;
            
            // Oblicz, co wyświetlić (obsługa poziomego scrolla)
            int len = row.length();
            if (len > editor.col_offset) {
                // Wyświetl fragment od col_offset
                // Ogranicz długość do szerokości ekranu, żeby nie zawijało
                int available_width = editor.screen_cols - 6; // 6 to szerokość numeracji
                std::string substr = row.substr(editor.col_offset, available_width);
                std::cout << substr;
            }
        } else {
            std::cout << "~"; // Styl Vima dla pustych linii
        }

        if (y < editor.screen_rows - 1) {
            std::cout << "\r\n";
        }
    }

    // Ustawienie kursora
    int line_number_offset = 6; 
    int screen_y = (editor.cursor_y - editor.row_offset) + 1;
    int screen_x = (editor.cursor_x - editor.col_offset) + 1 + line_number_offset;

    std::cout << "\x1b[" << screen_y << ";" << screen_x << "H";
    std::cout << "\x1b[?25h"; // Pokaż kursor
    std::cout.flush();
}

int editor_read_key(){
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1) return '\0';
    }

    if (c == '\x1b') {
        char seq[3];
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

        if (seq[0] == '[') {
            switch (seq[1]) {
                case 'A': return ARROW_UP;
                case 'B': return ARROW_DOWN;
                case 'C': return ARROW_RIGHT;
                case 'D': return ARROW_LEFT;
            }
        }
        return '\x1b';
    }
    return c;
}

// --- FUNKCJE EDYCJI (zaktualizowane dla std::string) ---

void editor_insert_newline(Editor &editor) {
    if (editor.lines.empty()) {
        editor.lines.push_back(Line(""));
    }
    
    std::string &current_row = editor.lines[editor.cursor_y].contents;
    
    // Podziel string na dwie części
    std::string next_row_content = "";
    if (editor.cursor_x < (int)current_row.length()) {
        next_row_content = current_row.substr(editor.cursor_x);
        current_row.erase(editor.cursor_x); // Usuń końcówkę z obecnej linii
    }

    // Wstaw nową linię do wektora
    editor.lines.insert(editor.lines.begin() + editor.cursor_y + 1, Line(next_row_content));
    
    editor.cursor_y++;
    editor.cursor_x = 0;
}

void editor_insert_char(Editor &editor, int c) {
    if (c == '\r' || c == '\n') {
        editor_insert_newline(editor);
        return;
    }
    
    if (editor.lines.empty()) {
        editor.lines.push_back(Line(""));
    }

    std::string &row = editor.lines[editor.cursor_y].contents;

    // Zabezpieczenie (choć string sam by rzucił wyjątek przy skrajnie błędnym indeksie)
    if (editor.cursor_x < 0) editor.cursor_x = 0;
    if (editor.cursor_x > (int)row.length()) editor.cursor_x = row.length();

    // Wstaw znak w odpowiednim miejscu
    row.insert(editor.cursor_x, 1, (char)c);
    editor.cursor_x++;
}

void editor_del_char(Editor &editor) {
    if (editor.lines.empty()) return;
    
    std::string &row = editor.lines[editor.cursor_y].contents;

    // 1. Kursor wewnątrz linii -> usuń znak przed kursorem
    if (editor.cursor_x > 0) {
        row.erase(editor.cursor_x - 1, 1);
        editor.cursor_x--;
    }
    // 2. Kursor na początku -> sklej z poprzednią linią
    else if (editor.cursor_y > 0) {
        std::string &prev_row = editor.lines[editor.cursor_y - 1].contents;
        
        // Zapisz pozycję kursora (koniec poprzedniej linii)
        int new_cursor_x = prev_row.length();
        
        // Doklej obecną linię do poprzedniej
        prev_row += row;
        
        // Usuń obecną linię
        editor.lines.erase(editor.lines.begin() + editor.cursor_y);
        
        // Przesuń kursor
        editor.cursor_y--;
        editor.cursor_x = new_cursor_x;
    }
}

// --- RUCH KURSORA ---

void editor_move_cursor(Editor &editor, int key) {
    // Pobierz długość aktualnej linii (lub 0 jeśli brak linii)
    int row_len = 0;
    if (editor.cursor_y < (int)editor.lines.size()) {
        row_len = editor.lines[editor.cursor_y].contents.length();
    }

    switch (key) {
        case ARROW_LEFT:
            if (editor.cursor_x != 0) {
                editor.cursor_x--;
            } else if (editor.cursor_y > 0) {
                // Opcjonalnie: przejście na koniec poprzedniej linii przy strzałce w lewo
                editor.cursor_y--;
                editor.cursor_x = editor.lines[editor.cursor_y].contents.length();
            }
            break;
        case ARROW_RIGHT:
            if (editor.cursor_x < row_len) {
                editor.cursor_x++;
            } else if (editor.cursor_x == row_len && editor.cursor_y < (int)editor.lines.size() - 1) {
                // Opcjonalnie: przejście na początek następnej linii
                editor.cursor_y++;
                editor.cursor_x = 0;
            }
            break;
        case ARROW_UP:
            if (editor.cursor_y != 0) {
                editor.cursor_y--;
            }
            break;
        case ARROW_DOWN:
            if (editor.cursor_y < (int)editor.lines.size() - 1) {
                editor.cursor_y++;
            }
            break;
    }
    
    // Korekta X po zmianie linii (np. skok z długiej na krótką)
    int new_row_len = 0;
    if (editor.cursor_y < (int)editor.lines.size()) {
        new_row_len = editor.lines[editor.cursor_y].contents.length();
    }
    if (editor.cursor_x > new_row_len) {
        editor.cursor_x = new_row_len;
    }
}

void editor_process_key_pressed(Editor &editor) {
  int c = editor_read_key();

  switch (c) {
    case CTRL_KEY('c'): 
            std::cout << "\x1b[2J\x1b[H";
            exit(0);
            break;
    case CTRL_KEY('s'):
        save_buffer(editor);
        break;
    case '\r':
    case '\n':
        editor_insert_newline(editor);
        break;
    case BACKSPACE: // 127
    case 8:         // Czasami backspace to 8 (Ctrl-H)
        editor_del_char(editor);
        break;
    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
        editor_move_cursor(editor, c);
        break;
    case '\x1b':
        break;
    default:
        editor_insert_char(editor, c);
        break;
  }
}

int main(int argc, char* argv[]){
    if(argc != 2){
        std::cout << "Usage: ./editor <filename>\n";
        return EXIT_FAILURE;
    }

    Editor editor(argv[1]);
    open_file(editor);

    enableRawMode();
    
    std::cout << "\x1b[2J"; // Clear screen

    while(true) {
        print_buffer(editor);
        editor_process_key_pressed(editor);
    }
    
    return 0;
}
