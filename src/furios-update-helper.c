#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <regex.h>

#define MAX_MATCH 10

void print_progress(const char* stage, int progress);
int parse_progress(const char* line, const char* pattern);
void run(const char* command, const char* stage);

void print_progress(const char* stage, int progress)
{
    printf("%s|%d\n", stage, progress);
    fflush(stdout);
}

int parse_progress(const char* line, const char* pattern)
{
    regex_t regex;
    regmatch_t match[MAX_MATCH];

    if (regcomp(&regex, pattern, REG_EXTENDED) != 0)
    {
        return -1;
    }

    if (regexec(&regex, line, MAX_MATCH, match, 0) == 0)
    {
        char number[16] = {0};
        strncpy(number, line + match[1].rm_so, match[1].rm_eo - match[1].rm_so);
        regfree(&regex);
        return atoi(number);
    }

    regfree(&regex);
    return -1;
}

void run(const char* command, const char* stage)
{
    FILE* pipe = popen(command, "r");
    if (!pipe)
    {
        fprintf(stderr, "Error executing command.\n");
        return;
    }

    char buffer[1024];
    int progress = 0;
    int total_packages = 0;
    int processed_packages = 0;

    while (fgets(buffer, sizeof(buffer), pipe) != NULL)
    {
        if (strstr(buffer, "packages can be upgraded"))
        {
            total_packages = parse_progress(buffer, "([0-9]+) packages can be upgraded");
        }
        else if (strstr(buffer, "Get:") || strstr(buffer, "Unpacking") || strstr(buffer, "Setting up"))
        {
            processed_packages++;
            if (total_packages > 0)
            {
                progress = (processed_packages * 100) / total_packages;
                print_progress(stage, progress);
            }
        }
        else if (strstr(buffer, "Reading package lists"))
        {
            progress = parse_progress(buffer, "Reading package lists\\.\\.\\. ([0-9]+)%");
            if (progress != -1) print_progress(stage, progress);
        }
        else if (strstr(buffer, "Building dependency tree"))
        {
            progress = parse_progress(buffer, "Building dependency tree\\.\\.\\. ([0-9]+)%");
            if (progress != -1) print_progress(stage, progress);
        }
        else if (strstr(buffer, "Calculating upgrade"))
        {
            print_progress(stage, 100);
        }
    }

    pclose(pipe);
}

int main()
{
    if (setuid(0) != 0)
    {
        perror("setuid");
        return 1;
    }

    setenv("LC_ALL", "C", 1);

    print_progress("check", 0);
    run("apt-get update", "check");

    print_progress("download", 0);
    run("apt-get upgrade --download-only -y", "download");

    print_progress("install", 0);
    run("apt-get upgrade -y", "install");

    printf("done\n");
    return 0;
}
