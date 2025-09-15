// calc.c - простой калькулятор для freestanding OS
typedef unsigned short u16;
volatile unsigned short* VGA = (unsigned short*)0xB8000;
int cursor_row = 0, cursor_col = 0;

void putchar(char c) {
    if (c == '\n') { cursor_col = 0; cursor_row++; return; }
    VGA[cursor_row*80 + cursor_col] = (c | (0x07 << 8));
    cursor_col++;
}

void prints(const char* s) {
    while (*s) putchar(*s++);
}

char read_char() {
    // простой метод ввода через порт клавиатуры (poll)
    unsigned char sc;
    while (1) {
        unsigned char status;
        __asm__ volatile ("inb $0x64, %0" : "=a"(status));
        if (status & 1) {
            __asm__ volatile ("inb $0x60, %0" : "=a"(sc));
            if (sc >= 0x02 && sc <= 0x0C) return "1234567890-="[sc-0x02];
            if (sc == 0x1C) return '\n';
        }
    }
}

int read_number() {
    int n = 0;
    char c;
    while ((c = read_char()) != '\n') {
        if (c >= '0' && c <= '9') {
            putchar(c);
            n = n*10 + (c-'0');
        }
    }
    return n;
}

void _start() {
    prints("Calc OS v0.1\nEnter 2 numbers:\n");
    int a = read_number();
    prints("\n");
    int b = read_number();
    prints("\nSum: ");
    int sum = a+b;
    if (sum == 0) { putchar('0'); } else {
        char buf[10]; int i=0;
        while(sum) { buf[i++] = '0' + (sum%10); sum/=10; }
        for(int j=i-1;j>=0;j--) putchar(buf[j]);
    }
    while(1) { __asm__ volatile("hlt"); }
}
