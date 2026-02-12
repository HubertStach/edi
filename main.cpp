#include <iostream>
#include <vector>
#include <fstream>
#include <sstream>
#include <cstring>
#include <unistd.h>

/* --windows-- */
#include <windows.h>
#include <conio.h>

DWORD originalConsoleMode;
HANDLE hStdin;

void disableRawMode() {
    SetConsoleMode(hStdin, originalConsoleMode);
}

void enableRawMode() {
    hStdin = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(hStdin, &originalConsoleMode);
    atexit(disableRawMode);

    DWORD rawMode = originalConsoleMode;
    rawMode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT);
    SetConsoleMode(hStdin, rawMode);
}

void enableAnsiSupport() {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, dwMode);
}
/* --windows-- */

#define CTRL_KEY(k) ((k) & 0x1f)

enum EditorKey {
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN
};

struct Line{
    char contents[80];

    Line(){
        clear();
    }

    void clear(){
        std::memset(contents, 0, 80);
    }
};

struct Editor{
    int cursor_x;
    int cursor_y;
    std::vector<Line> lines;
    std::string file_name;
    
    Editor(const std::string &file_name){
        this->file_name = file_name;
        this->cursor_x = 0;
        this->cursor_y = 0;
    }
};

void open_file(Editor &editor){
    std::fstream file(editor.file_name);

    if(!file.good()){
        file.close();
        std::ofstream file_cr(editor.file_name, std::ios::out);
        file_cr.close();
        open_file(editor);
    }
    else{
        std::stringstream content_stream;
        content_stream << file.rdbuf();
        file.close();
        std::string buffer = content_stream.str();

        std::vector<Line> lines;
        int col=0;
        Line temp_line;
        for(int i=0; i<buffer.length(); i++){
            char c = buffer.at(i);

            if (col >= 79 && c != '\n') {
                continue; 
            }

            if(c != '\n'){
                temp_line.contents[col] = c;
                col++;
            }
            else{
                temp_line.contents[col] = c;
                lines.push_back(temp_line);
                temp_line.clear();
                col = 0;
            }
        }

        if (col > 0) {
            temp_line.contents[col] = '\n';
            lines.push_back(temp_line);
        }
        editor.lines = lines;
    }
}

void save_buffer(const Editor &editor){
    std::ofstream file(editor.file_name);
    for(int row = 0; row < editor.lines.size(); row++){
        for(int col = 0; col < 80; col++){
            char c = editor.lines[row].contents[col];
            if(c == '\0') break; 
            file << c;
            if(c == '\n') break;
        }
    }
    file.close();
}

void print_buffer(const Editor &editor){
    std::cout << "\x1b[?25l";
    std::cout << "\x1b[H";

    for(int line = 0; line < editor.lines.size(); line++){
        std::cout << "\x1b[K";
        std::cout << line+1 << ":   "; 
        
        int col = 0;
        while(col < 80 && editor.lines[line].contents[col] != '\n' && editor.lines[line].contents[col] != '\0'){
            std::cout << editor.lines[line].contents[col];
            col++;
        }
        std::cout << "\r\n";
    }
    int line_number_offset = 5; 

    std::cout << "\x1b[" << (editor.cursor_y + 1) << ";" << (editor.cursor_x + 1 + line_number_offset) << "H";

    std::cout << "\x1b[?25h";
    std::cout.flush();
}

int editor_read_key(){
    int c = _getch();

    if (c == 0 || c == 224) {
        switch (_getch()) {
            case 72: return ARROW_UP;
            case 80: return ARROW_DOWN;
            case 75: return ARROW_LEFT;
            case 77: return ARROW_RIGHT;
        }
    }
    
    return c;
}

void editor_move_cursor(Editor &editor, int key) {
    int row_len = 0;
    if (editor.cursor_y < editor.lines.size()) {
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
             if (editor.cursor_x < 80 && editor.cursor_x < row_len) {
                editor.cursor_x++;
            }
            break;
        case ARROW_UP:
            if (editor.cursor_y != 0) {
                editor.cursor_y--;
            }
            break;
        case ARROW_DOWN:
            if (editor.cursor_y < editor.lines.size() - 1) {
                editor.cursor_y++;
            }
            break;
    }
    
    int new_row_len = 0;
    if (editor.cursor_y < editor.lines.size()) {
        new_row_len = strlen(editor.lines[editor.cursor_y].contents);
        if(new_row_len > 0 && editor.lines[editor.cursor_y].contents[new_row_len-1] == '\n') 
            new_row_len--;
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
    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
        editor_move_cursor(editor, c);
        break;
  }
}

int main(int argc, char* argv[]){
    if(argc != 2){
        std::cout<<"Please provide file\n";
        return EXIT_FAILURE;
    }

    Editor editor(argv[1]);
    open_file(editor);

    enableAnsiSupport();
    enableRawMode();
    
    std::cout << "\x1b[2J";

    while(true) {
        print_buffer(editor);
        editor_process_key_pressed(editor);
    }
}