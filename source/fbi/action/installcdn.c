#include <malloc.h>
#include <stdio.h>
#include <string.h>

#include <3ds.h>

#include "action.h"
#include "../resources.h"
#include "../task/uitask.h"
#include "../../core/core.h"

#define CONTENTS_MAX 256

typedef struct {
    list_item* item;
    ticket_info* ticket;
    volatile bool* done;
    bool finishedPrompt;

    char tmdVersion[16];
    u32 contentCount;
    u16 contentIndices[CONTENTS_MAX];
    u32 contentIds[CONTENTS_MAX];

    data_op_data installInfo;
} install_cdn_data;

static Result action_install_cdn_is_src_directory(void* data, u32 index, bool* isDirectory) {
    *isDirectory = false;
    return 0;
}

static Result action_install_cdn_make_dst_directory(void* data, u32 index) {
    return 0;
}

static Result action_install_cdn_open_src(void* data, u32 index, u32* handle) {
    install_cdn_data* installData = (install_cdn_data*) data;

    char url[256];
    if(index == 0) {
        if(strlen(installData->tmdVersion) > 0) {
            snprintf(url, 256, "http://ccs.cdn.c.shop.nintendowifi.net/ccs/download/%016llX/tmd.%s", installData->ticket->titleId, installData->tmdVersion);
        } else {
            snprintf(url, 256, "http://ccs.cdn.c.shop.nintendowifi.net/ccs/download/%016llX/tmd", installData->ticket->titleId);
        }
    } else {
        snprintf(url, 256, "http://ccs.cdn.c.shop.nintendowifi.net/ccs/download/%016llX/%08lX", installData->ticket->titleId, installData->contentIds[index - 1]);
    }

    return http_open((http_context*) handle, url, true);
}

static Result action_install_cdn_close_src(void* data, u32 index, bool succeeded, u32 handle) {
    return http_close((http_context) handle);
}

static Result action_install_cdn_get_src_size(void* data, u32 handle, u64* size) {
    u32 downloadSize = 0;
    Result res = http_get_size((http_context) handle, &downloadSize);

    *size = downloadSize;
    return res;
}

static Result action_install_cdn_read_src(void* data, u32 handle, u32* bytesRead, void* buffer, u64 offset, u32 size) {
    return http_read((http_context) handle, bytesRead, buffer, size);
}

static Result action_install_cdn_open_dst(void* data, u32 index, void* initialReadBlock, u64 size, u32* handle) {
    install_cdn_data* installData = (install_cdn_data*) data;

    if(index == 0) {
        installData->contentCount = tmd_get_content_count((u8*) initialReadBlock);
        if(installData->contentCount > CONTENTS_MAX) {
            return R_APP_OUT_OF_RANGE;
        }

        for(u32 i = 0; i < installData->contentCount; i++) {
            u8* contentChunk = tmd_get_content_chunk((u8*) initialReadBlock, i);

            installData->contentIds[i] = __builtin_bswap32(*(u32*) &contentChunk[0x00]);
            installData->contentIndices[i] = __builtin_bswap16(*(u16*) &contentChunk[0x04]);
        }

        installData->installInfo.total += installData->contentCount;

        return AM_InstallTmdBegin(handle);
    } else {
        return AM_InstallContentBegin(handle, installData->contentIndices[index - 1]);
    }
}

static Result action_install_cdn_close_dst(void* data, u32 index, bool succeeded, u32 handle) {
    install_cdn_data* installData = (install_cdn_data*) data;

    if(succeeded) {
        if(index == 0) {
            return AM_InstallTmdFinish(handle, true);
        } else {
            Result res = 0;
            if(R_SUCCEEDED(res = AM_InstallContentFinish(handle)) && index == 1 && installData->contentCount > 1 && (installData->ticket->titleId >> 32) == 0x0004008C) {
                FS_MediaType dest = fs_get_title_destination(installData->ticket->titleId);
                if(R_SUCCEEDED(res = AM_InstallTitleFinish())
                   && R_SUCCEEDED(res = AM_CommitImportTitles(dest, 1, false, &installData->ticket->titleId))
                   && R_SUCCEEDED(res = AM_InstallTitleBegin(dest, installData->ticket->titleId, false))) {
                    res = AM_CreateImportContentContexts(installData->contentCount - 1, &installData->contentIndices[1]);
                }
            }

            return res;
        }
    } else {
        if(index == 0) {
            return AM_InstallTmdAbort(handle);
        } else {
            return AM_InstallContentCancel(handle);
        }
    }
}

static Result action_install_cdn_write_dst(void* data, u32 handle, u32* bytesWritten, void* buffer, u64 offset, u32 size) {
    return FSFILE_Write(handle, bytesWritten, offset, buffer, size, 0);
}

static Result action_install_cdn_suspend_transfer(void* data, u32 index, u32* srcHandle, u32* dstHandle) {
    if(index > 0 && *dstHandle != 0) {
        return AM_InstallContentStop(*dstHandle);
    } else {
        return 0;
    }
}

static Result action_install_cdn_restore_transfer(void* data, u32 index, u32* srcHandle, u32* dstHandle) {
    install_cdn_data* installData = (install_cdn_data*) data;

    if(index > 0 && *dstHandle != 0) {
        return AM_InstallContentResume(dstHandle, &installData->installInfo.currProcessed, installData->contentIndices[index - 1]);
    } else {
        return 0;
    }
}

static Result action_install_cdn_suspend(void* data, u32 index) {
    return AM_InstallTitleStop();
}

static Result action_install_cdn_restore(void* data, u32 index) {
    install_cdn_data* installData = (install_cdn_data*) data;

    return AM_InstallTitleResume(fs_get_title_destination(installData->ticket->titleId), installData->ticket->titleId);
}

bool action_install_cdn_error(void* data, u32 index, Result res, ui_view** errorView) {
    install_cdn_data* installData = (install_cdn_data*) data;

    *errorView = error_display_res(installData->ticket, task_draw_ticket_info, res, "CDNからのインストールに失敗しました。 %s.", index == 0 ? "TMD" : "content");

    return false;
}

static void action_install_cdn_draw_top(ui_view* view, void* data, float x1, float y1, float x2, float y2) {
    task_draw_ticket_info(view, ((install_cdn_data*) data)->ticket, x1, y1, x2, y2);
}

static void action_install_cdn_free_data(install_cdn_data* data) {
    if(data->done != NULL) {
        *data->done = true;
    }

    free(data);
}

static void action_install_cdn_update(ui_view* view, void* data, float* progress, char* text) {
    install_cdn_data* installData = (install_cdn_data*) data;

    if(installData->installInfo.finished) {
        ui_pop();
        info_destroy(view);

        Result res = 0;

        if(R_SUCCEEDED(installData->installInfo.result)) {
            if(R_SUCCEEDED(res = AM_InstallTitleFinish())
               && R_SUCCEEDED(res = AM_CommitImportTitles(fs_get_title_destination(installData->ticket->titleId), 1, false, &installData->ticket->titleId))) {
                http_download_seed(installData->ticket->titleId);

                if(installData->ticket->titleId == 0x0004013800000002 || installData->ticket->titleId == 0x0004013820000002) {
                    res = AM_InstallFirm(installData->ticket->titleId);
                }
            }
        }

        if(R_SUCCEEDED(installData->installInfo.result) && R_SUCCEEDED(res)) {
            if(installData->finishedPrompt) {
                if(installData->item != NULL) {
                    task_populate_tickets_update_use(installData->item);
                }

                prompt_display_notify("成功", "インストールが完了しました。.", COLOR_TEXT, installData->ticket, task_draw_ticket_info, NULL);
            }
        } else {
            AM_InstallTitleAbort();

            if(R_FAILED(res)) {
                error_display_res(installData->ticket, task_draw_ticket_info, res, "CDNタイトルのインストールに失敗しました。");
            }
        }

        action_install_cdn_free_data(installData);

        return;
    }

    if((hidKeysDown() & KEY_B) && !installData->installInfo.finished) {
        svcSignalEvent(installData->installInfo.cancelEvent);
    }

    *progress = installData->installInfo.currTotal != 0 ? (float) ((double) installData->installInfo.currProcessed / (double) installData->installInfo.currTotal) : 0;
    snprintf(text, PROGRESS_TEXT_MAX, "%lu / %lu\n%.2f %s / %.2f %s\n%.2f %s/s, ETA %s", installData->installInfo.processed, installData->installInfo.total,
             ui_get_display_size(installData->installInfo.currProcessed),
             ui_get_display_size_units(installData->installInfo.currProcessed),
             ui_get_display_size(installData->installInfo.currTotal),
             ui_get_display_size_units(installData->installInfo.currTotal),
             ui_get_display_size(installData->installInfo.bytesPerSecond),
             ui_get_display_size_units(installData->installInfo.bytesPerSecond),
             ui_get_display_eta(installData->installInfo.estimatedRemainingSeconds));
}

static void action_install_cdn_n3ds_onresponse(ui_view* view, void* data, u32 response) {
    install_cdn_data* installData = (install_cdn_data*) data;

    if(response == PROMPT_YES) {
        FS_MediaType dest = fs_get_title_destination(installData->ticket->titleId);

        AM_DeleteTitle(dest, installData->ticket->titleId);
        if(dest == MEDIATYPE_SD) {
            AM_QueryAvailableExternalTitleDatabase(NULL);
        }

        Result res = 0;

        if(R_SUCCEEDED(res = AM_InstallTitleBegin(dest, installData->ticket->titleId, false))) {
            if(R_SUCCEEDED(res = task_data_op(&installData->installInfo))) {
                info_display("CDNタイトルのインストール", "Bを押してキャンセル", true, data, action_install_cdn_update, action_install_cdn_draw_top);
            } else {
                AM_InstallTitleAbort();
            }
        }

        if(R_FAILED(res)) {
            error_display_res(installData->ticket, task_draw_ticket_info, res, "CDNタイトルのインストールを開始できませんでした。");

            action_install_cdn_free_data(data);
        }
    } else {
        action_install_cdn_free_data(installData);
    }
}

static void action_install_cdn_version_onresponse(ui_view* view, void* data, SwkbdButton button, const char* response) {
    install_cdn_data* installData = (install_cdn_data*) data;

    if(button == SWKBD_BUTTON_CONFIRM) {
        strncpy(installData->tmdVersion, response, sizeof(installData->tmdVersion));

        bool n3ds = false;
        if(R_SUCCEEDED(APT_CheckNew3DS(&n3ds)) && !n3ds && ((installData->ticket->titleId >> 28) & 0xF) == 2) {
            prompt_display_yes_no("確認", "タイトルは、New3DSのシステムを対象としています。\n続けますか？", COLOR_TEXT, data, action_install_cdn_draw_top, action_install_cdn_n3ds_onresponse);
        } else {
            action_install_cdn_n3ds_onresponse(NULL, data, PROMPT_YES);
        }
    } else {
        action_install_cdn_free_data(installData);
    }
}

void action_install_cdn_noprompt_internal(volatile bool* done, ticket_info* info, bool finishedPrompt, bool promptVersion, list_item* item) {
    install_cdn_data* data = (install_cdn_data*) calloc(1, sizeof(install_cdn_data));
    if(data == NULL) {
        error_display(NULL, NULL, "インストールしたCDNデータの割り当てに失敗しました。");

        return;
    }

    data->item = item;
    data->ticket = info;
    data->done = done;
    data->finishedPrompt = finishedPrompt;

    memset(data->tmdVersion, '\0', sizeof(data->tmdVersion));

    data->contentCount = 0;
    memset(data->contentIndices, 0, sizeof(data->contentIndices));
    memset(data->contentIds, 0, sizeof(data->contentIds));

    data->installInfo.data = data;

    data->installInfo.op = DATAOP_COPY;

    data->installInfo.bufferSize = 128 * 1024;
    data->installInfo.copyEmpty = false;

    data->installInfo.total = 1;

    data->installInfo.isSrcDirectory = action_install_cdn_is_src_directory;
    data->installInfo.makeDstDirectory = action_install_cdn_make_dst_directory;

    data->installInfo.openSrc = action_install_cdn_open_src;
    data->installInfo.closeSrc = action_install_cdn_close_src;
    data->installInfo.getSrcSize = action_install_cdn_get_src_size;
    data->installInfo.readSrc = action_install_cdn_read_src;

    data->installInfo.openDst = action_install_cdn_open_dst;
    data->installInfo.closeDst = action_install_cdn_close_dst;
    data->installInfo.writeDst = action_install_cdn_write_dst;

    data->installInfo.suspendTransfer = action_install_cdn_suspend_transfer;
    data->installInfo.restoreTransfer = action_install_cdn_restore_transfer;

    data->installInfo.suspend = action_install_cdn_suspend;
    data->installInfo.restore = action_install_cdn_restore;

    data->installInfo.error = action_install_cdn_error;

    data->installInfo.finished = true;

    if(promptVersion) {
        kbd_display("バージョンを入力してください（デフォルトでは空です）", "", SWKBD_TYPE_NUMPAD, 0, SWKBD_ANYTHING, sizeof(data->tmdVersion), data, action_install_cdn_version_onresponse);
    } else {
        action_install_cdn_version_onresponse(NULL, data, SWKBD_BUTTON_CONFIRM, "");
    }
}

void action_install_cdn_noprompt(volatile bool* done, ticket_info* info, bool finishedPrompt, bool promptVersion) {
    action_install_cdn_noprompt_internal(done, info, finishedPrompt, promptVersion, NULL);
}

#define CDN_PROMPT_DEFAULT_VERSION 0
#define CDN_PROMPT_SELECT_VERSION 1
#define CDN_PROMPT_NO 2

static void action_install_cdn_onresponse(ui_view* view, void* data, u32 response) {
    if(response != CDN_PROMPT_NO) {
        list_item* item = (list_item*) data;

        action_install_cdn_noprompt_internal(NULL, (ticket_info*) item->data, true, response == CDN_PROMPT_SELECT_VERSION, item);
    }
}

void action_install_cdn(linked_list* items, list_item* selected) {
    static const char* options[3] = {"デフォルト\nバージョン", "選択\nバージョン", "いいえ"};
    static u32 optionButtons[3] = {KEY_A, KEY_X, KEY_B};
    prompt_display_multi_choice("確認", "選択したタイトルをCDNからインストールしますか？", COLOR_TEXT, options, optionButtons, 3, selected, task_draw_ticket_info, action_install_cdn_onresponse);
}