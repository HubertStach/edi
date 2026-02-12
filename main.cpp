#include <iostream>
#include <vector>
#include <fstream>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <termios.h>

//  g++ main.cpp -o edi

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
        for(size_t i=0; i<buffer.length(); i++){
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
    for(size_t row = 0; row < editor.lines.size(); row++){
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

    for(size_t line = 0; line < editor.lines.size(); line++){
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
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1) return '\0';
    }

    // \x1b to poczatek escape seq
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
        std::cout << "Please provide file\n";
        return EXIT_FAILURE;
    }

    Editor editor(argv[1]);
    open_file(editor);

    enableRawMode();
    
    std::cout << "\x1b[2J";

    while(true) {
        print_buffer(editor);
        editor_process_key_pressed(editor);
    }
    
    return 0;
}