#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <psp2/ctrl.h>
#include <psp2/appmgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/ctrl.h>
#include <psp2/system_param.h>
#include <psp2/rtc.h>
#include <psp2/shellutil.h>
#include <vita2d.h>

#include "common.h"
#include "config.h"
#include "appdb.h"
#include "console.h"
#include "file.h"
#include "font.h"
#include "button.h"

#define PAGE_ITEM_COUNT 20

enum {
    INJECTOR_MAIN = 1,
    INJECTOR_TITLE_SELECT,
    INJECTOR_BACKUP_PATCH,
    INJECTOR_START_DUMPER,
    INJECTOR_RESTORE_PATCH,
    INJECTOR_EXIT,
};

enum {
    DUMPER_MAIN = 1,
    DUMPER_SLOT_SELECT,
    DUMPER_BACKUP,
    DUMPER_RESTORE,
    DUMPER_DROP,
    DUMPER_FORMAT,
    DUMPER_FORMAT_CONFIRM,
    DUMPER_EXIT,
};

int ret;
int btn;
char *buf;
int buf_length;

char *concat(const char *str1, const char *str2) {
    char tmp[buf_length];
    snprintf(tmp, buf_length, "%s%s", str1, str2);
    memcpy(buf, tmp, buf_length);
    return buf;
}

int launch(const char *titleid) {
    char uri[32];
    sprintf(uri, "psgm:play?titleid=%s", titleid);

    sceKernelDelayThread(10000);
    sceAppMgrLaunchAppByUri(0xFFFFF, uri);
    sceKernelDelayThread(10000);
    sceAppMgrLaunchAppByUri(0xFFFFF, uri);

    unlock_psbutton();
    sceKernelExitProcess(0);

    return 0;
}

int cleanup_prev_inject(applist *list) {
    int fd = sceIoOpen(TEMP_FILE, SCE_O_RDONLY, 0777);
    if (fd <= 0) {
        return 0;
    }
    appinfo info;
    sceIoRead(fd, &info, sizeof(appinfo));
    sceIoClose(fd);

    sceIoRemove(TEMP_FILE);

    char backup[256];

    lock_psbutton();
    draw_start();
    if (is_encrypted_eboot(info.eboot)) {
        char patch[256];
        //char patch_eboot[256];
		sprintf(patch, "ux0:rePatch/%s", info.title_id);
        /*snprintf(patch, 256, "ux0:patch/%s", info.title_id);
        snprintf(patch_eboot, 256, "ux0:patch/%s/eboot.bin", info.title_id);
        if (!is_dumper_eboot(patch_eboot)) {
            unlock_psbutton();
            return 0;
        }*/

        draw_text(0, "Cleaning up old data...", white);
        ret = rmdir(patch);
        if (ret < 0) {
            draw_text(1, "Error", red);
            goto exit;
        }
        draw_text(1, "Done", white);

        /*snprintf(backup, 256, "ux0:patch/%s_orig", info.title_id);
        if (is_dir(backup)) {
            draw_text(3, "Restoring patch...", white);
            ret = mvdir(backup, patch);
            if (ret < 0) {
                draw_text(4, "Error", red);
                goto exit;
            }
            draw_text(4, "Done", white);
        }*/
        ret = 0;
    } else {
        draw_text(0, "Cleaning up old data...", white);
        snprintf(backup, 256, "%s.orig", info.eboot);
        ret = sceIoRemove(info.eboot);
        if (ret < 0) {
            draw_text(1, "Error", red);
            goto exit;
        }
        draw_text(1, "Done", white);
        draw_text(3, "Restoring eboot", white);
        ret = sceIoRename(backup, info.eboot);
        if (ret < 0) {
            draw_text(4, "Error", red);
            goto exit;
        }
        draw_text(4, "Done", white);
        ret = 0;
    }
exit:
    draw_end();
    unlock_psbutton();
    return ret;
}

int migrate_simple_rincheat_save() {
    char rindir_format[256];
    char old_savedir[256];
    snprintf(rindir_format, 256, "ux0:%s/%s",
             OLD_RINCHEAT_SAVEDIR, OLD_RINCHEAT_SAVE_FORMAT);
    snprintf(old_savedir, 256, rindir_format, app_titleid);
    if (!is_dir(old_savedir)) {
        return 0;
    }
    char new_dir[buf_length];
    for (int i = 0; i < 10; i++) {
        snprintf(new_dir, buf_length, config.full_path_format, app_titleid, i);
        if (is_dir(new_dir)) {
            continue;
        }
        mvdir(old_savedir, new_dir);
        return 1;
    }
    // TODO remove older remain save
    return -1;
}

int migrate_rincheat_slot_saves() {
    char rindir_format[256];
    char old_savedir[256];
    char new_dir[buf_length];
    snprintf(rindir_format, 256, "ux0:%s/%s",
             OLD_RINCHEAT_SAVEDIR, OLD_RINCHEAT_SAVE_SLOT_FORMAT);
    for (int i = 0; i < 10; i++) {
        snprintf(old_savedir, 256, rindir_format, app_titleid, i);
        if (!is_dir(old_savedir)) {
            continue;
        }
        for (int j = 0; j < 10; j++) {
            snprintf(new_dir, buf_length,
                     config.full_path_format, app_titleid, j);
            if (is_dir(new_dir)) {
                continue;
            }
            mvdir(old_savedir, new_dir);
            break;
        }
    }
    // TODO remove older remain saves
    return -1;
}

void print_game_list(appinfo *head, appinfo *tail, appinfo *curr) {
    appinfo *tmp = head;
    int i = 2;
    while (tmp) {
        snprintf(buf, 256, "%s: %s", tmp->title_id, tmp->title);
        draw_loop_text(i, buf, curr == tmp ? green : white);
        if (tmp == tail) {
            break;
        }
        tmp = tmp->next;
        i++;
    }
}

void make_save_slot_string(int slot) {
    char fn[buf_length];
    snprintf(fn, buf_length,
             concat(config.full_path_format, "/sce_sys/param.sfo"), app_titleid, slot);
    char date[20] = {0};
    if (!exists(fn)) {
        snprintf(date, 20, "none");
    } else {
        SceIoStat stat = {0};
        sceIoGetstat(fn, &stat);

        SceDateTime time;
        SceRtcTick tick_utc;
        SceRtcTick tick_local;
        sceRtcGetTick(&stat.st_mtime, &tick_utc);
        sceRtcConvertUtcToLocalTime(&tick_utc, &tick_local);
        sceRtcSetTick(&time, &tick_local);

        snprintf(date, 20, "%04d-%02d-%02d %02d:%02d:%02d",
                 time.year, time.month, time.day, time.hour, time.minute, time.second);
    }
    snprintf(buf, 256, "Slot %d - %s", slot, date);
}
void print_save_slots(int curr_slot) {
    int r = 4;
    for (int i = 0; i < 10; i++) {
        make_save_slot_string(i);
        draw_loop_text(r + i, buf, curr_slot == i ? green : white);
    }
}

#define DO_NOT_CLOSE_POPUP() \
    do { \
        popup_line lines[] = { \
            {.string="DO NOT CLOSE APPLICATION MANUALLY!", \
             .padding={20, 0, 20, 0}, .color=orange}, \
            {0}, \
        }; \
        open_popup(WARNING, lines); \
    } while (0)

#define ERROR_POPUP(msg) \
    do { \
        popup_line lines[] = { \
            {.string=""}, \
            {.string=(msg), .color=red}, \
            {.string=""}, \
            {0}, \
        }; \
        open_popup(ERROR, lines); \
    } while (0); \
    do { \
        btn = read_btn(); \
    } while (btn != SCE_CTRL_ENTER); \
    close_popup()

#define ERROR_POPUP2(msg1, msg2) \
    do { \
        popup_line lines[] = { \
            {.string=""}, \
            {.string=(msg1), .color=red}, \
            {.string=(msg2), .color=white}, \
            {.string=""}, \
            {0}, \
        }; \
        open_popup(ERROR, lines); \
    } while (0); \
    do { \
        btn = read_btn(); \
    } while (btn != SCE_CTRL_ENTER); \
    close_popup()

#define ERROR_CODE_POPUP(code) \
    do { \
        snprintf(buf, 256, "Error 0x%08X", ret); \
        ERROR_POPUP(buf); \
    } while (0)

int injector_main() {
    char version_string[256];
    snprintf(version_string, 256, "Vita Save Manager %s", VERSION);

    applist list = {0};

    ret = get_applist(&list);
    if (ret < 0) {
        draw_start();

        snprintf(buf, 256, "Initialization error, %x", ret);
        draw_text(0, buf, red);

        draw_end();

        while (read_btn());
        return -1;
    }
    appinfo *head, *tail, *curr;
    curr = head = tail = list.items;

    int i = 0;
    while (tail->next) {
        i++;
        if (i == PAGE_ITEM_COUNT) {
            break;
        }
        tail = tail->next;
    }

    int state = INJECTOR_MAIN;

    cleanup_prev_inject(&list);

#define draw_game_list() \
    do { \
        draw_loop_text(0, version_string, white); \
        print_game_list(head, tail, curr); \
        draw_loop_text(25, concat(ICON_UPDOWN, " - Select Item"), white); \
        draw_loop_text(26, concat(ICON_CANCEL, " - Exit"), white); \
    } while (0)

    while (1) {
        draw_start();

        switch (state) {
            case INJECTOR_MAIN:
                draw_game_list();

                btn = read_btn();
                if (btn == SCE_CTRL_ENTER) {
                    state = INJECTOR_TITLE_SELECT;
                    break;
                }
                if (btn == SCE_CTRL_CANCEL) {
                    state = INJECTOR_EXIT;
                    break;
                }
                if ((btn & SCE_CTRL_UP) && curr->prev) {
                    if (curr == head) {
                        head = head->prev;
                        tail = tail->prev;
                    }
                    curr = curr->prev;
                    break;
                }
                if ((btn & SCE_CTRL_DOWN) && curr->next) {
                    if (curr == tail) {
                        tail = tail->next;
                        head = head->next;
                    }
                    curr = curr->next;
                    break;
                }
                break;
            case INJECTOR_TITLE_SELECT:
                draw_game_list();

                do {
                    popup_line lines[] = {
                        {.string="Start save dumper?", .color=green},
                        {.string=""},
                        {.string="Selected:", .color=white},
                        {.string=curr->title_id, .padding={0, 0, 0, 20}, .color=white},
                        {.string=curr->title, .padding={0, 0, 0, 20}, .color=white},
                        {0},
                    };

                    draw_popup(CONFIRM_AND_CANCEL, lines);
                } while (0);

                btn = read_btn();
                if (btn == SCE_CTRL_ENTER) {
                    state = INJECTOR_START_DUMPER;
                } else if (btn == SCE_CTRL_CANCEL) {
                    state = INJECTOR_MAIN;
                }
                break;
            case INJECTOR_START_DUMPER:
                lock_psbutton();
                clear_screen();
                draw_text(0, version_string, white);

                char backup[256];

                // cartridge & digital encrypted games
                if (!exists(curr->eboot)) {
                    unlock_psbutton();
                    if (strcmp(curr->dev, "gro0") == 0) {
                        //draw_text(2, "Cartridge not inserted", red);
                        ERROR_POPUP("Cartridge not inserted");
                    } else {
                        //draw_text(2, "Cannot find game", red);
                        ERROR_POPUP("Cannot find game");
                    }
                    state = INJECTOR_MAIN;
                    break;
                }

                if (is_encrypted_eboot(curr->eboot)) {
                    char patch[256];
					char epatch[256];
                    draw_text(2, "Injecting (encrypted game)...", white);
                    //sprintf(patch, "ux0:patch/%s", curr->title_id);
                    //sprintf(backup, "ux0:patch/%s_orig", curr->title_id);
					sprintf(patch, "ux0:rePatch/%s", curr->title_id);
					sprintf(epatch, "ux0:rePatch/%s/eboot.bin", curr->title_id);
					if (!is_dir(patch))
						mkdir(patch, 0777);
                    //snprintf(buf, 255, "%s/eboot.bin", patch);
                    // need to backup patch dir
                    /*if (is_dir(patch) && !is_dumper_eboot(buf)) {
                        snprintf(buf, 255, "Backing up %s to %s...", patch, backup);
                        draw_text(4, buf, white);
                        rmdir(backup);
                        ret = mvdir(patch, backup);
                        if (ret < 0) {
                            unlock_psbutton();
                            ERROR_CODE_POPUP(ret);
                            state = INJECTOR_MAIN;
                            break;
                        }
                        draw_text(5, "Done", green);
                    }*/

                    // inject dumper to patch
                    snprintf(buf, 255, "Installing dumper to %s...", patch);
                    draw_text(7, buf, white);
                    //ret = copydir("ux0:app/SAVEMGR00", patch);
					ret = copyfile("ux0:app/SAVEMGR00/eboot.bin", epatch);
                    // TODO restore patch
                    if (ret < 0) {
                        unlock_psbutton();
                        ERROR_CODE_POPUP(ret);
                        state = INJECTOR_MAIN;
                        break;
                    }
                    draw_text(8, "Done", green);

                    /*snprintf(patch, 255, "ux0:patch/%s/sce_sys/param.sfo", curr->title_id);
                    //Restoring or Copying?
                    snprintf(buf, 255, "Copying param.sfo to %s...", patch);
                    draw_text(10, buf, white);

                    snprintf(buf, 255, "%s:app/%s/sce_sys/param.sfo", curr->dev, curr->title_id);
                    ret = copyfile(buf, patch);

                    if (ret < 0) {
                        unlock_psbutton();
                        ERROR_CODE_POPUP(ret);
                        state = INJECTOR_MAIN;
                        break;
                    }
                    draw_text(11, "Done", green);*/
                } else {
                    draw_text(2, "Injecting (decrypted game)...", white);
                    ret = -1;

                    if (strcmp(curr->dev, "gro0") == 0) {
                        unlock_psbutton();
                        ERROR_POPUP2("Game not supported", "Please send a bug report on github");
                        state = INJECTOR_MAIN;
                        break;
                    }

                    // vitamin or digital
                    snprintf(backup, 256, "%s.orig", curr->eboot);
                    snprintf(buf, 255, "Backing up %s to %s...", curr->eboot, backup);
                    draw_text(4, buf, white);
                    ret = sceIoRename(curr->eboot, backup);

                    if (ret < 0) {
                        unlock_psbutton();
                        ERROR_CODE_POPUP(ret);
                        state = INJECTOR_MAIN;
                        break;
                    }
                    draw_text(5, "Done", green);

                    snprintf(buf, 255, "Installing dumper to %s...", curr->eboot);
                    draw_text(7, buf, white);
                    ret = copyfile("ux0:app/SAVEMGR00/eboot.bin", curr->eboot);
                    // TODO if error, need restore eboot

                    if (ret < 0) {
                        unlock_psbutton();
                        ERROR_CODE_POPUP(ret);
                        state = INJECTOR_MAIN;
                        break;
                    }
                    draw_text(8, "Done", green);
                }

                // backup for next cleanup
                int fd = sceIoOpen(TEMP_FILE, SCE_O_WRONLY | SCE_O_CREAT,0777);
                sceIoWrite(fd, curr, sizeof(appinfo));
                sceIoClose(fd);

                DO_NOT_CLOSE_POPUP();

                // wait 3sec
                sceKernelDelayThread(3000000);

                close_popup();

                draw_text(15, "Starting dumper...", green);

                // TODO store state
                launch(curr->title_id);
                break;
            case INJECTOR_EXIT:
                draw_end();
                return 0;
        }

        draw_end();
    }
}

int dumper_main() {
    lock_psbutton();

    int state = DUMPER_MAIN;

    char save_dir[256], backup_dir[buf_length];

    char version_string[256];
    snprintf(version_string, 256, "Vita Save Manager %s", VERSION);

    appinfo info;

    int fd = sceIoOpen(TEMP_FILE, SCE_O_RDONLY, 0777);

    if (fd < 0) {
        draw_start();

        ERROR_POPUP("Cannot find inject data");
        state = DUMPER_EXIT;

        draw_end();
        launch(SAVE_MANAGER);
        return -1;
    }

    sceIoRead(fd, &info, sizeof(appinfo));
    sceIoClose(fd);

    if (strcmp(info.title_id, app_titleid) != 0) {
        draw_start();

        ERROR_POPUP("Wrong inject information");
        state = DUMPER_EXIT;

        draw_end();
        launch(SAVE_MANAGER);
        return -2;
    }

    if (strcmp(info.title_id, info.real_id) == 0) {
        sprintf(save_dir, "savedata0:");
    } else {
        sprintf(save_dir, "ux0:user/00/savedata/%s", info.real_id);
    }

    migrate_rincheat_slot_saves();
    if (strcmp(config.base, OLD_RINCHEAT_SAVEDIR) != 0 ||
            strcmp(config.slot_format, OLD_RINCHEAT_SAVE_SLOT_FORMAT) != 0) {
        migrate_simple_rincheat_save();
    }

    int slot = 0;

#define draw_dumper_header() \
    draw_text(0, version_string, white); \
    draw_text(2, "DO NOT CLOSE APPLICATION MANUALLY!", orange);

#define draw_save_stot() \
    do { \
        draw_loop_text(0, version_string, white); \
        draw_loop_text(2, "DO NOT CLOSE APPLICATION MANUALLY!", orange); \
        print_save_slots(slot); \
        draw_loop_text(25, concat(ICON_UPDOWN, " - Select Slot"), white); \
        draw_loop_text(26, concat(ICON_CANCEL, " - Exit"), white); \
    } while (0)

    while (1) {
        draw_start();

        snprintf(backup_dir, buf_length, config.full_path_format, info.title_id, slot);

        switch (state) {
            case DUMPER_MAIN:
                draw_save_stot();

                btn = read_btn();
                if (btn == SCE_CTRL_ENTER) {
                    state = DUMPER_SLOT_SELECT;
                    break;
                }
                if (btn == SCE_CTRL_CANCEL) {
                    state = DUMPER_EXIT;
                    break;
                }
                if (btn & (SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER)) {
                    state = DUMPER_FORMAT;
                    break;
                }
                if ((btn & SCE_CTRL_UP) && slot > 0) {
                    slot -= 1;
                    break;
                }
                if ((btn & SCE_CTRL_DOWN) && slot < 9) {
                    slot += 1;
                    break;
                }
                break;
            case DUMPER_SLOT_SELECT:
                draw_save_stot();

                do {
                    make_save_slot_string(slot);
                    char *slot_msg = strdup(buf);
                    char *dir_info = strdup(concat("Directory: ", backup_dir));

                    snprintf(buf, 256, "%s Close    %s Drop    %s Restore    %s Backup",
                             ICON_CANCEL, ICON_SQUARE, ICON_TRIANGLE, ICON_ENTER);
                    popup_line lines[] = {
                        {.string=slot_msg, .color=green},
                        {.string=""},
                        {.string=dir_info, .color=white},
                        {.string=""},
                        {.string=buf, .color=white, .align=CENTER},
                        {0},
                    };
                    draw_popup(SIMPLE, lines);
                    free(slot_msg);
                    free(dir_info);
                } while (0);

                btn = read_btn();
                if (btn == SCE_CTRL_ENTER) state = DUMPER_BACKUP;
                if (btn == SCE_CTRL_TRIANGLE) state = DUMPER_RESTORE;
                if (btn == SCE_CTRL_SQUARE) state = DUMPER_DROP;
                if (btn == SCE_CTRL_CANCEL) state = DUMPER_MAIN;
                break;
            case DUMPER_BACKUP:
                clear_screen();
                draw_dumper_header();

                snprintf(buf, 256, "Backing up to %s...", backup_dir);
                draw_text(4, buf, white);
                mkdir(backup_dir, 0777);
                ret = copydir(save_dir, backup_dir);

                if (ret < 0) {
                    ERROR_CODE_POPUP(ret);
                    state = DUMPER_SLOT_SELECT;
                    break;
                }
                draw_text(5, "Done", green);

                // wait 1sec
                sceKernelDelayThread(1000000);

                state = DUMPER_SLOT_SELECT;
                break;
            case DUMPER_RESTORE:
                clear_screen();
                draw_dumper_header();

                if (!is_dir(backup_dir)) {
                    ERROR_POPUP("Cannot find save data");
                    state = DUMPER_SLOT_SELECT;
                    break;
                }

                draw_text(4, "Remove old savedata0...", white);
                ret = rm_savedir(save_dir);

                draw_text(5, "Done", green);

                snprintf(buf, 256, "Restoring from %s...", backup_dir);
                draw_text(7, buf, white);
                ret = copydir(backup_dir, "savedata0:");

                if (ret < 0) {
                    ERROR_CODE_POPUP(ret);
                    state = DUMPER_SLOT_SELECT;
                    break;
                }
                draw_text(8, "Done", green);

                // wait 1sec
                sceKernelDelayThread(1000000);

                state = DUMPER_SLOT_SELECT;
                break;
            case DUMPER_DROP:
                clear_screen();
                draw_dumper_header();

                if (!is_dir(backup_dir)) {
                    ERROR_POPUP("Cannot find save data");
                    state = DUMPER_SLOT_SELECT;
                    break;
                }

                snprintf(buf, 256, "Remove %s...", backup_dir);
                draw_text(4, buf, white);
                ret = rmdir(backup_dir);
                if (ret < 0) {
                    ERROR_CODE_POPUP(ret);
                    state = DUMPER_SLOT_SELECT;
                    break;
                }
                draw_text(5, "Done", green);

                // wait 1sec
                sceKernelDelayThread(1000000);

                state = DUMPER_SLOT_SELECT;
                break;
            case DUMPER_FORMAT:
                draw_save_stot();

                do {
                    popup_line lines[] = {
                        {.string="Format savedata", .color=red},
                        {.string=""},
                        {.string="This action cannot be cancellation", .color=white},
                        {.string="Please have deep considering", .color=white},
                        {.string="And must dump savedata before this action", .color=white},
                        {0},
                    };
                    draw_popup(CONFIRM_AND_CANCEL, lines);
                } while (0);

                btn = read_btn();
                if (btn == SCE_CTRL_CANCEL) {
                    state = DUMPER_MAIN;
                    break;
                }
                if (btn == SCE_CTRL_ENTER) {
                    state = DUMPER_FORMAT_CONFIRM;
                    break;
                }
                break;
            case DUMPER_FORMAT_CONFIRM:
                clear_screen();
                draw_dumper_header();

                snprintf(buf, 256, "Formatting...");
                draw_text(4, "Formatting...", white);

                ret = rm_savedir(save_dir);
                if (ret < 0) {
                    ERROR_CODE_POPUP(ret);
                    state = DUMPER_SLOT_SELECT;
                    break;
                }

                draw_text(5, "Done", green);

                // wait 1sec
                sceKernelDelayThread(1000000);

                state = DUMPER_SLOT_SELECT;
                break;
            case DUMPER_EXIT:
                launch(SAVE_MANAGER);
                break;
        }
        draw_end();
    }
}

int main() {
    vita2d_init();
    init_console();
    load_config();
    sceShellUtilInitEvents(0);

    buf_length = strlen(config.full_path_format) + 64;
    buf = malloc(sizeof(char) * buf_length);

    mkdir(concat("ux0:", config.base), 0777);
    sceIoMkdir("ux0:/data/savemgr", 0777);

    if (strcmp(app_titleid, SAVE_MANAGER) == 0) {
        injector_main();
    } else {
        dumper_main();
    }
    sceKernelExitProcess(0);
}
