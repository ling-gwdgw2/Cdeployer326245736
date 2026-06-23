#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_LINE 512
#define MAX_VAL 256

// Configuration values
int git_enabled = 0;
char git_branch[64] = "main";
char commit_message[MAX_VAL] = "";
char build_command[MAX_VAL] = "";
char deploy_command[MAX_VAL] = "";

void trim(char *str) {
    int len = strlen(str);
    while (len > 0 && (str[len - 1] == ' ' || str[len - 1] == '\t' || str[len - 1] == '\r' || str[len - 1] == '\n')) {
        str[len - 1] = '\0';
        len--;
    }
    int start = 0;
    while (str[start] == ' ' || str[start] == '\t') {
        start++;
    }
    if (start > 0) {
        memmove(str, str + start, strlen(str + start) + 1);
    }
}

void parse_config(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        // Create default config file
        printf("\033[33m[Warning] Configuration file '%s' not found. Creating a default configuration...\033[0m\n", filename);
        file = fopen(filename, "w");
        if (file) {
            fprintf(file, "# DeployFlow Configuration File\n");
            fprintf(file, "# Set to 1 to auto add, commit, and push to Git\n");
            fprintf(file, "git_enabled = 1\n");
            fprintf(file, "git_branch = main\n");
            fprintf(file, "commit_message = Auto-deployed via deployer.exe\n\n");
            fprintf(file, "# Build command (comment out or leave blank if none)\n");
            fprintf(file, "build_command = npm run build\n\n");
            fprintf(file, "# Deployment command (comment out or leave blank if none)\n");
            fprintf(file, "deploy_command = firebase deploy\n");
            fclose(file);
            printf("\033[32m[Success] Created '%s'. Please edit it and run deployer.exe again.\033[0m\n", filename);
            exit(0);
        } else {
            printf("\033[31m[Error] Could not create configuration file '%s'.\033[0m\n", filename);
            exit(1);
        }
    }

    char line[MAX_LINE];
    while (fgets(line, sizeof(line), file)) {
        trim(line);
        // Skip comments and empty lines
        if (line[0] == '#' || line[0] == ';' || line[0] == '\0') {
            continue;
        }

        char *eq = strchr(line, '=');
        if (!eq) continue;

        *eq = '\0';
        char *key = line;
        char *val = eq + 1;

        trim(key);
        trim(val);

        if (strcmp(key, "git_enabled") == 0) {
            git_enabled = atoi(val);
        } else if (strcmp(key, "git_branch") == 0) {
            strncpy(git_branch, val, sizeof(git_branch) - 1);
        } else if (strcmp(key, "commit_message") == 0) {
            strncpy(commit_message, val, sizeof(commit_message) - 1);
        } else if (strcmp(key, "build_command") == 0) {
            strncpy(build_command, val, sizeof(build_command) - 1);
        } else if (strcmp(key, "deploy_command") == 0) {
            strncpy(deploy_command, val, sizeof(deploy_command) - 1);
        }
    }
    fclose(file);
}

int run_system_cmd(const char *cmd) {
    printf("\033[36mExecuting: %s\033[0m\n", cmd);
    int status = system(cmd);
    if (status != 0) {
        printf("\033[31mCommand failed with status code: %d\033[0m\n", status);
        return status;
    }
    return 0;
}

int main() {
    // Enable ANSI escape code support on Windows consoles
    #ifdef _WIN32
    system("color"); // Simple trick to enable VT processing in cmd.exe session
    #endif

    printf("\033[1;35m========================================\033[0m\n");
    printf("\033[1;35m        DEPLOYFLOW AUTO-DEPLOYER        \033[0m\n");
    printf("\033[1;35m========================================\033[0m\n\n");

    const char *config_name = "deploy.conf";
    parse_config(config_name);

    printf("\033[34m[Info] Loaded Configuration:\033[0m\n");
    printf("  Git Enabled:    %s\n", git_enabled ? "Yes" : "No");
    if (git_enabled) {
        printf("  Git Branch:     %s\n", git_branch);
        printf("  Commit Message: %s\n", commit_message[0] ? commit_message : "[Auto Timestamp]");
    }
    printf("  Build Command:  %s\n", build_command[0] ? build_command : "[None]");
    printf("  Deploy Command: %s\n\n", deploy_command[0] ? deploy_command : "[None]");

    // --- STEP 1: GIT ---
    if (git_enabled) {
        printf("\033[1;33m--- Step 1: Git Push ---\033[0m\n");
        
        // Check if git directory exists
        FILE *git_check = fopen(".git/config", "r");
        if (!git_check) {
            printf("\033[33m[Warning] Git repository not initialized. Initializing...\033[0m\n");
            if (run_system_cmd("git init") != 0) {
                printf("\033[31m[Error] Failed to initialize Git.\033[0m\n");
                // Wait for key before exit
                printf("\nPress Enter to exit...");
                getchar();
                return 1;
            }
        } else {
            fclose(git_check);
        }

        if (run_system_cmd("git add -A") != 0) {
            printf("\033[31m[Error] Failed to stage changes.\033[0m\n");
            printf("\nPress Enter to exit...");
            getchar();
            return 1;
        }

        char commit_cmd[MAX_LINE + 128];
        if (commit_message[0]) {
            snprintf(commit_cmd, sizeof(commit_cmd), "git commit -m \"%s\"", commit_message);
        } else {
            time_t rawtime;
            struct tm *timeinfo;
            char time_str[80];
            time(&rawtime);
            timeinfo = localtime(&rawtime);
            strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", timeinfo);
            snprintf(commit_cmd, sizeof(commit_cmd), "git commit -m \"Auto deploy: %s\"", time_str);
        }
        
        // Git commit returns 1 if there's nothing to commit, we can ignore that failure
        printf("Running commit...\n");
        system(commit_cmd);

        char push_cmd[128];
        snprintf(push_cmd, sizeof(push_cmd), "git push origin %s", git_branch);
        if (run_system_cmd(push_cmd) != 0) {
            printf("\033[31m[Error] Failed to push to remote repository.\033[0m\n");
            printf("\nPress Enter to exit...");
            getchar();
            return 1;
        }
        printf("\033[32m[Git] Git step completed successfully.\033[0m\n\n");
    }

    // --- STEP 2: BUILD ---
    if (build_command[0]) {
        printf("\033[1;33m--- Step 2: Build ---\033[0m\n");
        if (run_system_cmd(build_command) != 0) {
            printf("\033[31m[Error] Build step failed.\033[0m\n");
            printf("\nPress Enter to exit...");
            getchar();
            return 1;
        }
        printf("\033[32m[Build] Build step completed successfully.\033[0m\n\n");
    }

    // --- STEP 3: DEPLOY ---
    if (deploy_command[0]) {
        printf("\033[1;33m--- Step 3: Deploy ---\033[0m\n");
        if (run_system_cmd(deploy_command) != 0) {
            printf("\033[31m[Error] Deployment step failed.\033[0m\n");
            printf("\nPress Enter to exit...");
            getchar();
            return 1;
        }
        printf("\033[32m[Deploy] Deployment step completed successfully.\033[0m\n\n");
    }

    printf("\033[1;32m========================================\033[0m\n");
    printf("\033[1;32m🎉 DEPLOYMENT COMPLETED SUCCESSFULLY! 🎉\033[0m\n");
    printf("\033[1;32m========================================\033[0m\n");

    // Pause before exiting so console stays open on Windows double click
    printf("\nPress Enter to exit...");
    getchar();

    return 0;
}
