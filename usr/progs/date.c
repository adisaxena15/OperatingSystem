
#include "syscall.h"
#include "string.h"
#define STDOUT 1
#define CONSOLEOUT 2
static const int days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
static const char* month_names[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", 
                                     "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
int is_leap_year(int year) {
    if (year % 400 == 0) return 1;
    if (year % 100 == 0) return 0;
    if (year % 4 == 0) return 1;
    return 0;
}
int get_days_in_month(int month, int year) {
    if (month == 1 && is_leap_year(year)) {
        return 29;
    }
    return days_in_month[month];
}
void main(int argc, char* argv[]) {
    unsigned long long ns;
    unsigned long long seconds;
    int fd;
    int result;
    long bytes_read;
    fd = 3;
    result = _open(fd, "dev/rtc0");
    if (result < 0) {
        _write(CONSOLEOUT, "date: cannot open RTC device\n", 30);
        _exit();
    }
    bytes_read = _read(fd, &ns, sizeof(ns));
    if (bytes_read != sizeof(ns)) {
        _write(CONSOLEOUT, "date: cannot read from RTC\n", 27);
        _close(fd);
        _exit();
    }
    _close(fd);
    seconds = ns / 1000000000ULL;
    int sec = seconds % 60;
    seconds /= 60;
    int min = seconds % 60;
    seconds /= 60;
    int hour = seconds % 24;
    seconds /= 24;
    unsigned long days = seconds;
    int year = 1970;
    while (1) {
        int days_in_year = is_leap_year(year) ? 366 : 365;
        if (days < days_in_year) break;
        days -= days_in_year;
        year++;
    }
    int month = 0;
    while (days >= get_days_in_month(month, year)) {
        days -= get_days_in_month(month, year);
        month++;
    }
    int day = days + 1;
    printf("%02d %s %d %02d:%02d:%02d\n", day, month_names[month], year, hour, min, sec);
    _exit();
}
