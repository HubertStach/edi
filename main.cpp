#include <iostream>
#include <vector>
#include <fstream>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <iomanip> // Do std::setw

/* -- Linux Terminal Config -- */
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
/* -------------------------- */

#define CTRL_KEY(k) ((k) & 0x1f)

// Funkcja pomocnicza do pobierania rozmiaru okna
int get_window_size(int *rows, int *cols) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        return -1;
    }
    else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

enum EditorKey {
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN
};

struct Line {
    char contents[80];

    Line(){
        clear();
    }

    void clear(){
        std::memset(contents, 0, 80);
    }
};

struct Editor {
    int cursor_x;
    int cursor_y;
    int row_offset;
    int screen_rows;
    int screen_cols;
    std::vector<Line> lines;
    std::string file_name;
    
    Editor(const std::string &file_name){
        this->file_name = file_name;
        this->cursor_x = 0;
        this->cursor_y = 0;
        this->row_offset = 0;
        
        // Pobierz rozmiar terminala przy starcie
        if (get_window_size(&screen_rows, &screen_cols) == -1) {
            screen_rows = 24;
            screen_cols = 80;
        }
    }
};

void open_file(Editor &editor){
    std::fstream file(editor.file_name);

    if(!file.good()){
        file.close();
        std::ofstream file_cr(editor.file_name, std::ios::out);
        file_cr.close();
        editor.lines.clear();
        editor.lines.push_back(Line());
    }
    else{
        std::stringstream content_stream;
        content_stream << file.rdbuf();
        file.close();
        std::string buffer = content_stream.str();

        std::vector<Line> lines;
        int col = 0;
        Line temp_line;

        for(size_t i = 0; i < buffer.length(); i++){
            char c = buffer.at(i);

            if (col >= 79 && c != '\n') {
                continue; 
            }

            if(c == '\n'){
                lines.push_back(temp_line);
                temp_line.clear();
                col = 0;
            }
            else {
                temp_line.contents[col] = c;
                col++;
            }
        }
        lines.push_back(temp_line);
        
        editor.lines = lines;
    }
    
    if (editor.lines.empty()) {
        editor.lines.push_back(Line());
    }
}

void save_buffer(const Editor &editor){
    std::ofstream file(editor.file_name);
    for(size_t row = 0; row < editor.lines.size(); row++){
        file << editor.lines[row].contents;
        
        file << '\n';
    }
    file.close();
}

void editor_scroll(Editor &editor) {
    if (editor.cursor_y < editor.row_offset) {
        editor.row_offset = editor.cursor_y;
    }

    if (editor.cursor_y >= editor.row_offset + editor.screen_rows) {
        editor.row_offset = editor.cursor_y - editor.screen_rows + 1;
    }
}

void print_buffer(Editor &editor){
    editor_scroll(editor);

    std::cout << "\x1b[?25l";
    std::cout << "\x1b[H";

    for(int y = 0; y < editor.screen_rows; y++){
        int file_row = y + editor.row_offset;

        std::cout << "\x1b[K";

        if (file_row < (int)editor.lines.size()) {
            std::cout <<(file_row + 1) << ":\t|"; 
            
            int col = 0;
            while(col < 80 && editor.lines[file_row].contents[col] != '\n' && editor.lines[file_row].contents[col] != '\0'){
                std::cout << editor.lines[file_row].contents[col];
                col++;
            }
        } else {
            std::cout << "~";
        }
        if (y < editor.screen_rows - 1) {
            std::cout << "\r\n";
        }
    }

    int line_number_offset = 9; 

    int screen_y = (editor.cursor_y - editor.row_offset) + 1;
    int screen_x = (editor.cursor_x + 1 + line_number_offset);

    std::cout << "\x1b[" << screen_y << ";" << screen_x << "H";
    
    std::cout << "\x1b[?25h";
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

void editor_insert_newline(Editor &editor) {
    if (editor.cursor_x > 80) editor.cursor_x = 80;

    Line new_line;
    
    if (editor.cursor_y < (int)editor.lines.size()) {
        char* row = editor.lines[editor.cursor_y].contents;
        int len = strlen(row);
        
        if (editor.cursor_x < len) {
            strcpy(new_line.contents, &row[editor.cursor_x]);
            row[editor.cursor_x] = '\0';
        }
    }

    editor.lines.insert(editor.lines.begin() + editor.cursor_y + 1, new_line);
    
    editor.cursor_y++;
    editor.cursor_x = 0;
}

void editor_insert_char(Editor &editor, int c) {
    if (c == '\r' || c == '\n') {
        editor_insert_newline(editor);
        return;
    }

    if (editor.lines.empty()) {
        editor.lines.push_back(Line());
    }

    char* row = editor.lines[editor.cursor_y].contents;
    int len = strlen(row);

    if (len >= 79) return;

    memmove(&row[editor.cursor_x + 1], &row[editor.cursor_x], len - editor.cursor_x + 1);
    
    row[editor.cursor_x] = (char)c;
    editor.cursor_x++;
}

void editor_del_char(Editor &editor) {
    if (editor.lines.empty()) return;

    if (editor.cursor_x > 0) {
        char* row = editor.lines[editor.cursor_y].contents;
        int len = strlen(row);
        
        memmove(&row[editor.cursor_x - 1], &row[editor.cursor_x], len - editor.cursor_x + 1);
        editor.cursor_x--;
    }
    else if (editor.cursor_y > 0) {
        char* curr_row = editor.lines[editor.cursor_y].contents;
        char* prev_row = editor.lines[editor.cursor_y - 1].contents;
        
        int curr_len = strlen(curr_row);
        int prev_len = strlen(prev_row);
        if (prev_len + curr_len < 80) {
            editor.cursor_x = prev_len;
            
            strcat(prev_row, curr_row);
            
            editor.lines.erase(editor.lines.begin() + editor.cursor_y);
            
            editor.cursor_y--;
        }
    }
}

void editor_move_cursor(Editor &editor, int key) {
    size_t row_len = 0;
    if (editor.cursor_y < (int)editor.lines.size()) {
        row_len = strlen(editor.lines[editor.cursor_y].contents);
        if (row_len > 0 && editor.lines[editor.cursor_y].contents[row_len-1] == '\n') 
            row_len--;
    }

    switch (key) {
        case ARROW_LEFT:
            if (editor.cursor_x != 0) {
                editor.cursor_x--;
            }
            break;
        case ARROW_RIGHT:
             if (editor.cursor_x < 80 && editor.cursor_x < (int)row_len) {
                editor.cursor_x++;
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
    
    // Korekta pozycji X po zmianie linii (jeśli nowa linia jest krótsza)
    size_t new_row_len = 0;
    if (editor.cursor_y < (int)editor.lines.size()) {
        new_row_len = strlen(editor.lines[editor.cursor_y].contents);
        if(new_row_len > 0 && editor.lines[editor.cursor_y].contents[new_row_len-1] == '\n') 
            new_row_len--;
    }
    if (editor.cursor_x > (int)new_row_len) {
        editor.cursor_x = (int)new_row_len;
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

        case 127:
        case 8:
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
        std::cout << "Please provide file\n";
        return EXIT_FAILURE;
    }

    Editor editor(argv[1]);
    open_file(editor);

    enableRawMode();
    
    std::cout << "\x1b[2J"; // Wyczyść ekran raz na początku

    while(true) {
        print_buffer(editor);
        editor_process_key_pressed(editor);
    }
    
    return 0;
}