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
char active_config_file[MAX_VAL] = "deploy.conf";

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

// 1. Local Logging Helper
void write_log(const char *message) {
    FILE *log_file = fopen("deploy.log", "a");
    if (log_file) {
        time_t rawtime;
        struct tm *timeinfo;
        char time_str[80];
        time(&rawtime);
        timeinfo = localtime(&rawtime);
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", timeinfo);
        
        fprintf(log_file, "[%s] %s\n", time_str, message);
        fclose(log_file);
    }
}

// 2. Windows Toast Notification Helper
void send_windows_notification(const char *title, const char *message, int is_error) {
    char ps_cmd[1024];
    const char *icon_type = is_error ? "Error" : "Info";
    // Using powershell system tip to show balloon notification on Windows
    snprintf(ps_cmd, sizeof(ps_cmd),
        "powershell -NoProfile -WindowStyle Hidden -Command \"Add-Type -AssemblyName System.Windows.Forms; "
        "$n = New-Object System.Windows.Forms.NotifyIcon; $n.Icon = [System.Drawing.SystemIcons]::Information; "
        "$n.Visible = $true; $n.ShowBalloonTip(5000, '%s', '%s', [System.Windows.Forms.ToolTipIcon]::%s);\"",
        title, message, icon_type);
    system(ps_cmd);
}

// 3. Pre-flight check functions
int file_exists(const char *path) {
    FILE *file = fopen(path, "r");
    if (file) {
        fclose(file);
        return 1;
    }
    return 0;
}

int command_exists(const char *cmd_line) {
    char first_word[64];
    int i = 0;
    // Extract first word, skipping leading spaces if any
    while (cmd_line[i] && (cmd_line[i] == ' ' || cmd_line[i] == '\t')) {
        i++;
    }
    int j = 0;
    while (cmd_line[i] && cmd_line[i] != ' ' && cmd_line[i] != '\t' && j < 63) {
        first_word[j++] = cmd_line[i++];
    }
    first_word[j] = '\0';

    if (first_word[0] == '\0' || strcmp(first_word, "echo") == 0 || strcmp(first_word, "cd") == 0) {
        return 1; // mock command or internal shells
    }

    char check_cmd[128];
    snprintf(check_cmd, sizeof(check_cmd), "where %s >nul 2>&1", first_word);
    return (system(check_cmd) == 0);
}

void auto_detect_commands() {
    if (strcmp(build_command, "auto") == 0 || build_command[0] == '\0') {
        if (file_exists("package.json")) {
            strcpy(build_command, "npm run build");
            printf("\033[32m[Auto-Detect] Found package.json -> Build Command: %s\033[0m\n", build_command);
        } else if (file_exists("Cargo.toml")) {
            strcpy(build_command, "cargo build --release");
            printf("\033[32m[Auto-Detect] Found Cargo.toml -> Build Command: %s\033[0m\n", build_command);
        } else if (file_exists("pom.xml")) {
            strcpy(build_command, "mvn clean package");
            printf("\033[32m[Auto-Detect] Found pom.xml -> Build Command: %s\033[0m\n", build_command);
        } else if (file_exists("build.gradle")) {
            strcpy(build_command, "gradlew build");
            printf("\033[32m[Auto-Detect] Found build.gradle -> Build Command: %s\033[0m\n", build_command);
        } else if (file_exists("Makefile")) {
            strcpy(build_command, "make");
            printf("\033[32m[Auto-Detect] Found Makefile -> Build Command: %s\033[0m\n", build_command);
        } else {
            if (strcmp(build_command, "auto") == 0) {
                build_command[0] = '\0';
            }
        }
    }

    if (strcmp(deploy_command, "auto") == 0 || deploy_command[0] == '\0') {
        if (file_exists("firebase.json")) {
            strcpy(deploy_command, "firebase deploy");
            printf("\033[32m[Auto-Detect] Found firebase.json -> Deploy Command: %s\033[0m\n", deploy_command);
        } else if (file_exists("vercel.json")) {
            strcpy(deploy_command, "vercel --prod");
            printf("\033[32m[Auto-Detect] Found vercel.json -> Deploy Command: %s\033[0m\n", deploy_command);
        } else if (file_exists("netlify.toml")) {
            strcpy(deploy_command, "netlify deploy --prod");
            printf("\033[32m[Auto-Detect] Found netlify.toml -> Deploy Command: %s\033[0m\n", deploy_command);
        } else if (file_exists("wrangler.toml")) {
            strcpy(deploy_command, "npx wrangler deploy");
            printf("\033[32m[Auto-Detect] Found wrangler.toml -> Deploy Command: %s\033[0m\n", deploy_command);
        } else if (file_exists("sst.config.ts") || file_exists("sst.json")) {
            strcpy(deploy_command, "npx sst deploy --stage prod");
            printf("\033[32m[Auto-Detect] Found SST config -> Deploy Command: %s\033[0m\n", deploy_command);
        } else {
            if (strcmp(deploy_command, "auto") == 0) {
                deploy_command[0] = '\0';
            }
        }
    }
}

void parse_config(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        printf("\033[33m[Warning] Configuration file '%s' not found. Creating a default configuration...\033[0m\n", filename);
        file = fopen(filename, "w");
        if (file) {
            fprintf(file, "# DeployFlow Configuration File\n");
            fprintf(file, "# Set to 1 to auto add, commit, and push to Git\n");
            fprintf(file, "git_enabled = 1\n");
            fprintf(file, "git_branch = main\n");
            fprintf(file, "commit_message = Auto-deployed via deployer.exe\n\n");
            fprintf(file, "# Build command (set to 'auto' to auto-detect, or specify command, leave blank if none)\n");
            fprintf(file, "build_command = auto\n\n");
            fprintf(file, "# Deployment command (set to 'auto' to auto-detect, or specify command, leave blank if none)\n");
            fprintf(file, "deploy_command = auto\n");
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

int main(int argc, char *argv[]) {
    #ifdef _WIN32
    system("color");
    #endif

    printf("\033[1;35m========================================\033[0m\n");
    printf("\033[1;35m        DEPLOYFLOW AUTO-DEPLOYER        \033[0m\n");
    printf("\033[1;35m========================================\033[0m\n\n");

    // Environment support
    if (argc > 1) {
        snprintf(active_config_file, sizeof(active_config_file), "deploy.%s.conf", argv[1]);
        printf("\033[34m[Info] Using Environment: %s\033[0m\n", argv[1]);
        if (!file_exists(active_config_file)) {
            printf("\033[31m[Error] Configuration file '%s' not found!\033[0m\n", active_config_file);
            printf("\nPress Enter to exit...");
            getchar();
            return 1;
        }
    } else {
        printf("\033[34m[Info] Using Default Environment\033[0m\n");
    }

    parse_config(active_config_file);
    auto_detect_commands();

    // Log Start
    char start_log_msg[MAX_VAL + 128];
    snprintf(start_log_msg, sizeof(start_log_msg), "Starting deployment sequence (Config: %s)...", active_config_file);
    write_log(start_log_msg);

    // --- PRE-FLIGHT CHECKS ---
    printf("\033[1;33m--- Pre-flight Quality Checks ---\033[0m\n");
    write_log("Running pre-flight checks...");

    // Check internet connection
    printf("Checking internet connection...\n");
    int internet_ok = (system("ping -n 1 8.8.8.8 >nul 2>&1") == 0);
    if (!internet_ok) {
        printf("\033[33m[Pre-flight Warning] No internet connection detected (ping to 8.8.8.8 failed).\033[0m\n");
        printf("\033[33m                    Deployments requiring network may fail.\033[0m\n");
        write_log("Pre-flight: Warning - No internet connection detected.");
    } else {
        printf("\033[32m[Pre-flight] Internet connection OK.\033[0m\n");
    }

    // Check git tool
    if (git_enabled) {
        if (!command_exists("git")) {
            printf("\033[31m[Pre-flight Error] 'git' tool was not found in your system PATH.\033[0m\n");
            printf("\033[31m                   Please install Git or disable git_enabled in config.\033[0m\n");
            write_log("Pre-flight: Failed - 'git' tool missing.");
            send_windows_notification("DeployFlow Error", "Deployment failed: 'git' tool not found in system PATH.", 1);
            printf("\nPress Enter to exit...");
            getchar();
            return 1;
        }
        printf("\033[32m[Pre-flight] 'git' tool check passed.\033[0m\n");
    }

    // Check build command executable
    if (build_command[0]) {
        if (!command_exists(build_command)) {
            printf("\033[33m[Pre-flight Warning] The build command executable in '%s' might not be installed.\033[0m\n", build_command);
            write_log("Pre-flight: Warning - Build tool might be missing.");
        } else {
            printf("\033[32m[Pre-flight] Build tool check passed.\033[0m\n");
        }
    }

    // Check deploy command executable
    if (deploy_command[0]) {
        if (!command_exists(deploy_command)) {
            printf("\033[33m[Pre-flight Warning] The deploy command executable in '%s' might not be installed.\033[0m\n", deploy_command);
            write_log("Pre-flight: Warning - Deploy tool might be missing.");
        } else {
            printf("\033[32m[Pre-flight] Deploy tool check passed.\033[0m\n");
        }
    }
    printf("\033[32m[Pre-flight] Pre-flight checks finished.\033[0m\n\n");

    // --- STEP 1: GIT ---
    if (git_enabled) {
        printf("\033[1;33m--- Step 1: Git Push ---\033[0m\n");
        write_log("Git: Starting Git commit & push...");

        FILE *git_check = fopen(".git/config", "r");
        if (!git_check) {
            printf("\033[33m[Warning] Git repository not initialized. Initializing...\033[0m\n");
            write_log("Git: Initializing Git repository...");
            if (run_system_cmd("git init") != 0) {
                printf("\033[31m[Error] Failed to initialize Git.\033[0m\n");
                write_log("Git: Failed - Initialization failed.");
                send_windows_notification("DeployFlow Error", "Deployment failed at Git initialization.", 1);
                printf("\nPress Enter to exit...");
                getchar();
                return 1;
            }
        } else {
            fclose(git_check);
        }

        if (run_system_cmd("git add -A") != 0) {
            printf("\033[31m[Error] Failed to stage changes.\033[0m\n");
            write_log("Git: Failed - Staging files failed.");
            send_windows_notification("DeployFlow Error", "Deployment failed at staging files.", 1);
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
        
        printf("Running commit...\n");
        system(commit_cmd);

        char push_cmd[128];
        snprintf(push_cmd, sizeof(push_cmd), "git push origin %s", git_branch);
        if (run_system_cmd(push_cmd) != 0) {
            printf("\033[31m[Error] Failed to push to remote repository.\033[0m\n");
            write_log("Git: Failed - Push to remote failed.");
            send_windows_notification("DeployFlow Error", "Deployment failed at Git push.", 1);
            printf("\nPress Enter to exit...");
            getchar();
            return 1;
        }
        printf("\033[32m[Git] Git step completed successfully.\033[0m\n\n");
        write_log("Git: Completed successfully.");
    }

    // --- STEP 2: BUILD ---
    if (build_command[0]) {
        printf("\033[1;33m--- Step 2: Build ---\033[0m\n");
        write_log("Build: Running build command...");
        if (run_system_cmd(build_command) != 0) {
            printf("\033[31m[Error] Build step failed.\033[0m\n");
            write_log("Build: Failed - Command failed.");
            send_windows_notification("DeployFlow Error", "Deployment failed at Build step.", 1);
            printf("\nPress Enter to exit...");
            getchar();
            return 1;
        }
        printf("\033[32m[Build] Build step completed successfully.\033[0m\n\n");
        write_log("Build: Completed successfully.");
    }

    // --- STEP 3: DEPLOY ---
    if (deploy_command[0]) {
        printf("\033[1;33m--- Step 3: Deploy ---\033[0m\n");
        write_log("Deploy: Running deploy command...");
        if (run_system_cmd(deploy_command) != 0) {
            printf("\033[31m[Error] Deployment step failed.\033[0m\n");
            write_log("Deploy: Failed - Command failed.");
            send_windows_notification("DeployFlow Error", "Deployment failed at Deploy step.", 1);
            printf("\nPress Enter to exit...");
            getchar();
            return 1;
        }
        printf("\033[32m[Deploy] Deployment step completed successfully.\033[0m\n\n");
        write_log("Deploy: Completed successfully.");
    }

    printf("\033[1;32m========================================\033[0m\n");
    printf("\033[1;32m🎉 DEPLOYMENT COMPLETED SUCCESSFULLY! 🎉\033[0m\n");
    printf("\033[1;32m========================================\033[0m\n");

    // Success notifications & logging
    write_log("Deployment: Success! Entire flow finished.");
    send_windows_notification("DeployFlow Success", "Congratulations! Your project has been deployed successfully.", 0);

    printf("\nPress Enter to exit...");
    getchar();

    return 0;
}
