#ifndef COMMANDS_H
#define COMMANDS_H

#include "common.h"

void registerCommands();
void dispatchCommand(char *line, bool fromSerial);
void handle_boot(char *args, bool fromSerial);
bool isSystemProtected(const char* name);


void handle_ls(char *args, bool fromSerial);
void handle_cat(char *args, bool fromSerial);
void handle_cd(char *args, bool fromSerial);
void handle_pwd(char *args, bool fromSerial);
void handle_echo(char *args, bool fromSerial);
void handle_login(char *args, bool fromSerial);
void handle_help(char *args, bool fromSerial);
void handle_on(char *args, bool fromSerial);
void handle_off(char *args, bool fromSerial);
void handle_pinmode(char *args, bool fromSerial);
void handle_write(char *args, bool fromSerial);
void handle_read(char *args, bool fromSerial);
void handle_neofetch(char *args, bool fromSerial);
void handle_uptime(char *args, bool fromSerial);
void handle_free(char *args, bool fromSerial);
void handle_mkdir(char *args, bool fromSerial);
void handle_touch(char *args, bool fromSerial);
void handle_rm(char *args, bool fromSerial);
void handle_mv(char *args, bool fromSerial);
void handle_cp(char *args, bool fromSerial);

void handle_reboot(char *args, bool fromSerial);
void handle_i2c(char *args, bool fromSerial);
void handle_date(char *args, bool fromSerial);
void handle_wifi(char *args, bool fromSerial);
void handle_clear(char *args, bool fromSerial);
void handle_dmesg(char *args, bool fromSerial);
void handle_df(char *args, bool fromSerial);
void handle_hwinfo(char *args, bool fromSerial);
void handle_logout(char *args, bool fromSerial);
void handle_exit(char *args, bool fromSerial);
void handle_accel(char *args, bool fromSerial);
void handle_hf(char *args, bool fromSerial);
void handle_chat(char *args, bool fromSerial);
void handle_sh(char *args, bool fromSerial);
void handle_color(char *args, bool fromSerial);
void handle_whoami(char *args, bool fromSerial);
void handle_uname(char *args, bool fromSerial);
void handle_passwd(char *args, bool fromSerial);
void handle_alias(char *args, bool fromSerial);
void handle_env(char *args, bool fromSerial);
void handle_export(char *args, bool fromSerial);
void handle_sys(char *args, bool fromSerial);
void handle_ps(char *args, bool fromSerial);
void handle_top(char *args, bool fromSerial);
void handle_append(char *args, bool fromSerial);
void handle_info(char *args, bool fromSerial);
void handle_save(char *args, bool fromSerial);
void handle_load(char *args, bool fromSerial);
void handle_lfs(char *args, bool fromSerial);
void handle_chmod(char *args, bool fromSerial);
void handle_chown(char *args, bool fromSerial);
void handle_cpu(char *args, bool fromSerial);
void handle_sleep(char *args, bool fromSerial);
void handle_deepsleep(char *args, bool fromSerial);
void handle_firewall(char *args, bool fromSerial);
void handle_ota(char *args, bool fromSerial);
void handle_delay(char *args, bool fromSerial);
void handle_kill(char *args, bool fromSerial);
void handle_trigger(char *args, bool fromSerial);
void handle_mqtt(char *args, bool fromSerial);
void handle_pwm(char *args, bool fromSerial);
void handle_gpio(char *args, bool fromSerial);
void handle_ping(char *args, bool fromSerial);
void handle_wget(char *args, bool fromSerial);
void handle_ntp(char *args, bool fromSerial);
void handle_telnet(char *args, bool fromSerial);
void handle_web(char *args, bool fromSerial);
void handle_ssh(char *args, bool fromSerial);
void handle_bt(char *args, bool fromSerial);
void handle_netstat(char *args, bool fromSerial);
void handle_cron(char *args, bool fromSerial);
void handle_bg(char *args, bool fromSerial);

void handle_waitwifi(char *args, bool fromSerial);
void handle_recovery(char *args, bool fromSerial);

#endif