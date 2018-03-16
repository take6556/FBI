#include <malloc.h>

#include <3ds.h>

#include "action.h"
#include "../resources.h"
#include "../task/uitask.h"
#include "../../core/core.h"

typedef struct {
    linked_list* items;
    list_item* selected;
} delete_ext_save_data_data;

static void action_delete_ext_save_data_draw_top(ui_view* view, void* data, float x1, float y1, float x2, float y2) {
    task_draw_ext_save_data_info(view, ((delete_ext_save_data_data*) data)->selected->data, x1, y1, x2, y2);
}

static void action_delete_ext_save_data_update(ui_view* view, void* data, float* progress, char* text) {
    delete_ext_save_data_data* deleteData = (delete_ext_save_data_data*) data;
    ext_save_data_info* info = (ext_save_data_info*) deleteData->selected->data;

    FS_ExtSaveDataInfo extInfo = {.mediaType = info->mediaType, .saveId = info->extSaveDataId};
    Result res = FSUSER_DeleteExtSaveData(extInfo);

    ui_pop();
    info_destroy(view);

    if(R_FAILED(res)) {
        error_display_res(info, task_draw_ext_save_data_info, res, "拡張セーブデータの削除に失敗しました。");
    } else {
        linked_list_remove(deleteData->items, deleteData->selected);
        task_free_ext_save_data(deleteData->selected);

        prompt_display_notify("成功", "拡張セーブデータが削除されました。", COLOR_TEXT, NULL, NULL, NULL);
    }

    free(data);
}

static void action_delete_ext_save_data_onresponse(ui_view* view, void* data, u32 response) {
    if(response == PROMPT_YES) {
        info_display("拡張セーブデータの削除", "", false, data, action_delete_ext_save_data_update, action_delete_ext_save_data_draw_top);
    } else {
        free(data);
    }
}

void action_delete_ext_save_data(linked_list* items, list_item* selected) {
    delete_ext_save_data_data* data = (delete_ext_save_data_data*) calloc(1, sizeof(delete_ext_save_data_data));
    if(data == NULL) {
        error_display(NULL, NULL, "削除した拡張セーブデータのデータの割り当てに失敗しました。");

        return;
    }

    data->items = items;
    data->selected = selected;

    prompt_display_notify("確認", "選択した拡張セーブデータを削除しますか？", COLOR_TEXT, data, action_delete_ext_save_data_draw_top, action_delete_ext_save_data_onresponse);
}