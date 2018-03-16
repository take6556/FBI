#include <malloc.h>
#include <stdio.h>
#include <string.h>

#include <3ds.h>

#include "resources.h"
#include "section.h"
#include "action/action.h"
#include "task/uitask.h"
#include "../core/core.h"

static list_item install = {"インストール", COLOR_TEXT, action_install_titledb};

typedef struct {
    populate_titledb_data populateData;

    bool showCIAs;
    bool show3DSXs;
    bool sortByName;
    bool sortByUpdate;

    bool populated;
} titledb_data;

typedef struct {
    titledb_data* parent;
    linked_list* items;
} titledb_options_data;

typedef struct {
    linked_list* items;
    list_item* selected;
} titledb_entry_data;

typedef struct {
    linked_list* items;
    list_item* selected;
    bool cia;
} titledb_action_data;

static void titledb_action_draw_top(ui_view* view, void* data, float x1, float y1, float x2, float y2, list_item* selected) {
    titledb_action_data* actionData = (titledb_action_data*) data;

    if(actionData->cia) {
        task_draw_titledb_info_cia(view, actionData->selected->data, x1, y1, x2, y2);
    } else {
        task_draw_titledb_info_tdsx(view, actionData->selected->data, x1, y1, x2, y2);
    }
}

static void titledb_action_update(ui_view* view, void* data, linked_list* items, list_item* selected, bool selectedTouched) {
    titledb_action_data* actionData = (titledb_action_data*) data;

    if(hidKeysDown() & KEY_B) {
        ui_pop();
        list_destroy(view);

        free(data);

        return;
    }

    if(selected != NULL && selected->data != NULL && (selectedTouched || (hidKeysDown() & KEY_A))) {
        void(*action)(linked_list*, list_item*, bool) = (void(*)(linked_list*, list_item*, bool)) selected->data;

        ui_pop();
        list_destroy(view);

        action(actionData->items, actionData->selected, actionData->cia);

        free(data);

        return;
    }

    if(linked_list_size(items) == 0) {
        linked_list_add(items, &install);
    }
}

static void titledb_action_open(linked_list* items, list_item* selected, bool cia) {
    titledb_action_data* data = (titledb_action_data*) calloc(1, sizeof(titledb_action_data));
    if(data == NULL) {
        error_display(NULL, NULL, "TitleDBのアクションデータの割り当てに失敗しました。");

        return;
    }

    data->items = items;
    data->selected = selected;
    data->cia = cia;

    list_display("TitleDBアクション", "A: 選択, B: 戻る", data, titledb_action_update, titledb_action_draw_top);
}

static void titledb_entry_draw_top(ui_view* view, void* data, float x1, float y1, float x2, float y2, list_item* selected) {
    titledb_entry_data* entryData = (titledb_entry_data*) data;

    if(selected != NULL) {
        if(strncmp(selected->name, "CIA", sizeof(selected->name)) == 0) {
            task_draw_titledb_info_cia(view, entryData->selected->data, x1, y1, x2, y2);
        } else if(strncmp(selected->name, "3DSX", sizeof(selected->name)) == 0) {
            task_draw_titledb_info_tdsx(view, entryData->selected->data, x1, y1, x2, y2);
        }
    }
}

static void titledb_entry_update(ui_view* view, void* data, linked_list* items, list_item* selected, bool selectedTouched) {
    titledb_entry_data* entryData = (titledb_entry_data*) data;

    if(hidKeysDown() & KEY_B) {
        ui_pop();

        linked_list_iter iter;
        linked_list_iterate(items, &iter);

        while(linked_list_iter_has_next(&iter)) {
            free(linked_list_iter_next(&iter));
            linked_list_iter_remove(&iter);
        }

        list_destroy(view);
        free(data);

        return;
    }

    if(selected != NULL && (selectedTouched || (hidKeysDown() & KEY_A))) {
        titledb_action_open(entryData->items, entryData->selected, (bool) selected->data);
        return;
    }

    titledb_info* info = (titledb_info*) entryData->selected->data;
    if(linked_list_size(items) == 0) {
        if(info->cia.exists) {
            list_item* item = (list_item*) calloc(1, sizeof(list_item));
            if(item != NULL) {
                strncpy(item->name, "CIA", sizeof(item->name));
                item->data = (void*) true;
                item->color = info->cia.installed ? info->cia.installedInfo.id != info->cia.id ? COLOR_TITLEDB_OUTDATED : COLOR_TITLEDB_INSTALLED : COLOR_TITLEDB_NOT_INSTALLED;

                linked_list_add(items, item);
            }
        }

        if(info->tdsx.exists) {
            list_item* item = (list_item*) calloc(1, sizeof(list_item));
            if(item != NULL) {
                strncpy(item->name, "3DSX", sizeof(item->name));
                item->data = (void*) false;
                item->color = info->tdsx.installed ? info->tdsx.installedInfo.id != info->tdsx.id ? COLOR_TITLEDB_OUTDATED : COLOR_TITLEDB_INSTALLED : COLOR_TITLEDB_NOT_INSTALLED;

                linked_list_add(items, item);
            }
        }
    } else {
        linked_list_iter iter;
        linked_list_iterate(items, &iter);

        while(linked_list_iter_has_next(&iter)) {
            list_item* item = (list_item*) linked_list_iter_next(&iter);

            if((bool) item->data) {
                item->color = info->cia.installed ? info->cia.installedInfo.id != info->cia.id ? COLOR_TITLEDB_OUTDATED : COLOR_TITLEDB_INSTALLED : COLOR_TITLEDB_NOT_INSTALLED;
            } else {
                item->color = info->tdsx.installed ? info->tdsx.installedInfo.id != info->tdsx.id ? COLOR_TITLEDB_OUTDATED : COLOR_TITLEDB_INSTALLED : COLOR_TITLEDB_NOT_INSTALLED;
            }
        }
    }
}

static void titledb_entry_open(linked_list* items, list_item* selected) {
    titledb_entry_data* data = (titledb_entry_data*) calloc(1, sizeof(titledb_entry_data));
    if(data == NULL) {
        error_display(NULL, NULL, "TitleDBのエントリデータの割り当てに失敗しました。");

        return;
    }

    data->items = items;
    data->selected = selected;

    list_display("TitleDBエントリー", "A: 選択, B: 戻る", data, titledb_entry_update, titledb_entry_draw_top);
}

static int titledb_compare(void* data, const void* p1, const void* p2) {
    titledb_data* listData = (titledb_data*) data;

    list_item* info1 = (list_item*) p1;
    list_item* info2 = (list_item*) p2;

    titledb_info* data1 = (titledb_info*) info1->data;
    titledb_info* data2 = (titledb_info*) info2->data;

    if(listData->sortByName) {
        return strncasecmp(info1->name, info2->name, sizeof(info1->name));
    } else if(listData->sortByUpdate) {
        return strncasecmp(data2->updatedAt, data1->updatedAt, sizeof(data2->updatedAt));
    } else {
        return 0;
    }
}

static void titledb_options_add_entry(linked_list* items, const char* name, bool* val) {
    list_item* item = (list_item*) calloc(1, sizeof(list_item));
    if(item != NULL) {
        snprintf(item->name, LIST_ITEM_NAME_MAX, "%s", name);
        item->color = *val ? COLOR_ENABLED : COLOR_DISABLED;
        item->data = val;

        linked_list_add(items, item);
    }
}

static void titledb_options_update(ui_view* view, void* data, linked_list* items, list_item* selected, bool selectedTouched) {
    titledb_options_data* optionsData = (titledb_options_data*) data;
    titledb_data* listData = optionsData->parent;

    if(hidKeysDown() & KEY_B) {
        linked_list_iter iter;
        linked_list_iterate(items, &iter);

        while(linked_list_iter_has_next(&iter)) {
            free(linked_list_iter_next(&iter));
            linked_list_iter_remove(&iter);
        }

        ui_pop();
        list_destroy(view);

        return;
    }

    if(selected != NULL && selected->data != NULL && (selectedTouched || (hidKeysDown() & KEY_A))) {
        bool* val = (bool*) selected->data;
        *val = !(*val);

        if(val == &listData->sortByName || val == &listData->sortByUpdate) {
            if(*val) {
                if(val == &listData->sortByName) {
                    listData->sortByUpdate = false;
                } else if(val == &listData->sortByUpdate) {
                    listData->sortByName = false;
                }

                linked_list_iter iter;
                linked_list_iterate(items, &iter);
                while(linked_list_iter_has_next(&iter)) {
                    list_item* item = (list_item*) linked_list_iter_next(&iter);

                    item->color = *(bool*) item->data ? COLOR_ENABLED : COLOR_DISABLED;
                }
            } else {
                selected->color = *val ? COLOR_ENABLED : COLOR_DISABLED;
            }

            linked_list_sort(optionsData->items, listData, titledb_compare);
        } else {
            selected->color = *val ? COLOR_ENABLED : COLOR_DISABLED;

            listData->populated = false;
        }
    }

    if(linked_list_size(items) == 0) {
        titledb_options_add_entry(items, "CIAを表示", &listData->showCIAs);
        titledb_options_add_entry(items, "3DSXを表示", &listData->show3DSXs);
        titledb_options_add_entry(items, "名前順に並び替え", &listData->sortByName);
        titledb_options_add_entry(items, "アップデートされた日付順に並び替え", &listData->sortByUpdate);
    }
}

static void titledb_options_open(titledb_data* parent, linked_list* items) {
    titledb_options_data* data = (titledb_options_data*) calloc(1, sizeof(titledb_options_data));
    if(data == NULL) {
        error_display(NULL, NULL, "TitleDBのオプションのデータの割り当てに失敗しました。");

        return;
    }

    data->parent = parent;
    data->items = items;

    list_display("オプション", "A: 変更, B: 戻る", data, titledb_options_update, NULL);
}

static void titledb_draw_top(ui_view* view, void* data, float x1, float y1, float x2, float y2, list_item* selected) {
    titledb_data* listData = (titledb_data*) data;

    if(!listData->populateData.itemsListed) {
        static const char* text = "タイトルリストを読み込んでいます。お待ちください...\n注意: キャンセルには最大15秒かかります。";

        float textWidth;
        float textHeight;
        screen_get_string_size(&textWidth, &textHeight, text, 0.5f, 0.5f);
        screen_draw_string(text, x1 + (x2 - x1 - textWidth) / 2, y1 + (y2 - y1 - textHeight) / 2, 0.5f, 0.5f, COLOR_TEXT, true);
    } else if(selected != NULL && selected->data != NULL) {
        task_draw_titledb_info(view, selected->data, x1, y1, x2, y2);
    }
}

static void titledb_update(ui_view* view, void* data, linked_list* items, list_item* selected, bool selectedTouched) {
    titledb_data* listData = (titledb_data*) data;

    svcSignalEvent(listData->populateData.resumeEvent);

    if(hidKeysDown() & KEY_B) {
        if(!listData->populateData.finished) {
            svcSignalEvent(listData->populateData.cancelEvent);
            while(!listData->populateData.finished) {
                svcSleepThread(1000000);
            }
        }

        ui_pop();

        task_clear_titledb(items);
        list_destroy(view);

        free(listData);
        return;
    }

    if(!listData->populated || (hidKeysDown() & KEY_X)) {
        if(!listData->populateData.finished) {
            svcSignalEvent(listData->populateData.cancelEvent);
            while(!listData->populateData.finished) {
                svcSleepThread(1000000);
            }
        }

        listData->populateData.items = items;
        Result res = task_populate_titledb(&listData->populateData);
        if(R_FAILED(res)) {
            error_display_res(NULL, NULL, res, "TitleDBのリストの作成の開始に失敗しました。");
        }

        listData->populated = true;
    }

    if(listData->populateData.itemsListed) {
        if(hidKeysDown() & KEY_Y) {
            action_update_titledb(items, selected);
            return;
        }

        if(hidKeysDown() & KEY_SELECT) {
            titledb_options_open(listData, items);
            return;
        }
    }

    if(listData->populateData.finished && R_FAILED(listData->populateData.result)) {
        error_display_res(NULL, NULL, listData->populateData.result, "TitleDBのリストの作成に失敗しました。");

        listData->populateData.result = 0;
    }

    if(selected != NULL && selected->data != NULL && (selectedTouched || (hidKeysDown() & KEY_A))) {
        svcClearEvent(listData->populateData.resumeEvent);

        titledb_entry_open(items, selected);
        return;
    }
}

static bool titledb_filter(void* data, titledb_info* info) {
    titledb_data* listData = (titledb_data*) data;

    return (info->cia.exists && listData->showCIAs) || (info->tdsx.exists && listData->show3DSXs);
}

void titledb_open() {
    titledb_data* data = (titledb_data*) calloc(1, sizeof(titledb_data));
    if(data == NULL) {
        error_display(NULL, NULL, "TitleDBのデータの割り当てに失敗しました。");

        return;
    }

    data->showCIAs = true;
    data->show3DSXs = true;
    data->sortByName = true;
    data->sortByUpdate = false;

    data->populateData.finished = true;

    data->populateData.userData = data;
    data->populateData.filter = titledb_filter;
    data->populateData.compare = titledb_compare;

    list_display("TitleDB.com", "A: 選択, B: 戻る, X: 一覧をリロード, Y: 全てをアップデート, Select: オプション", data, titledb_update, titledb_draw_top);
}