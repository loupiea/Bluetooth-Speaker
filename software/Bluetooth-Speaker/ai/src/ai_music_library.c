/* 自动生成：不要手动修改。运行 tools/generate_music_library.py 更新。 */
#include "ai_music_library.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static const ai_music_track_t s_tracks[] = {
    {
        .name = "好想爱这个世界啊",
        .artist = "DL.白纸×Rc.椿和",
        .url = "http://lubancat.local:8081/music/DL.%E7%99%BD%E7%BA%B8%C3%97Rc.%E6%A4%BF%E5%92%8C-%E5%A5%BD%E6%83%B3%E7%88%B1%E8%BF%99%E4%B8%AA%E4%B8%96%E7%95%8C%E5%95%8A.wav",
    },
    {
        .name = "安九",
        .artist = "DL.阿俊×排骨教主",
        .url = "http://lubancat.local:8081/music/DL.%E9%98%BF%E4%BF%8A%C3%97%E6%8E%92%E9%AA%A8%E6%95%99%E4%B8%BB-%E5%AE%89%E4%B9%9D%20.wav",
    },
    {
        .name = "今晚不想睡",
        .artist = "Nagi×Rs素素×Xy.苹果鱼",
        .url = "http://lubancat.local:8081/music/Nagi%C3%97Rs%E7%B4%A0%E7%B4%A0%C3%97Xy.%E8%8B%B9%E6%9E%9C%E9%B1%BC-%E4%BB%8A%E6%99%9A%E4%B8%8D%E6%83%B3%E7%9D%A1.wav",
    },
    {
        .name = "情蛊+囍(1)",
        .artist = "R.kik ×DL.毒",
        .url = "http://lubancat.local:8081/music/R.kik%20%C3%97DL.%E6%AF%92-%E6%83%85%E8%9B%8A%2B%E5%9B%8D%281%29.wav",
    },
    {
        .name = "登神（GODS）",
        .artist = "Tony×西索",
        .url = "http://lubancat.local:8081/music/Tony%C3%97%E8%A5%BF%E7%B4%A2-%E7%99%BB%E7%A5%9E%EF%BC%88GODS%EF%BC%89.wav",
    },
    {
        .name = "Marry you",
        .artist = "Xy.Blue×Xy.LayenM",
        .url = "http://lubancat.local:8081/music/Xy.Blue%C3%97Xy.LayenM-Marry%20you.wav",
    },
    {
        .name = "Leave the door open",
        .artist = "Xy.kai×R.涵",
        .url = "http://lubancat.local:8081/music/Xy.kai%C3%97R.%E6%B6%B5-Leave%20the%20door%20open.wav",
    },
    {
        .name = "四面楚歌+懦夫+Look At Me Now+霍元甲",
        .artist = "Xy.Ro7×Xy.Feng",
        .url = "http://lubancat.local:8081/music/Xy.Ro7%C3%97Xy.Feng-%E5%9B%9B%E9%9D%A2%E6%A5%9A%E6%AD%8C%2B%E6%87%A6%E5%A4%AB%2BLook%20At%20Me%20Now%2B%E9%9C%8D%E5%85%83%E7%94%B2.wav",
    },
    {
        .name = "诀爱·尽",
        .artist = "Xy.小皇少×Rc.柯东",
        .url = "http://lubancat.local:8081/music/Xy.%E5%B0%8F%E7%9A%87%E5%B0%91%C3%97Rc.%E6%9F%AF%E4%B8%9C-%E8%AF%80%E7%88%B1%C2%B7%E5%B0%BD.wav",
    },
    {
        .name = "光亮",
        .artist = "xy.小超×小凡",
        .url = "http://lubancat.local:8081/music/xy.%E5%B0%8F%E8%B6%85%C3%97%E5%B0%8F%E5%87%A1-%E5%85%89%E4%BA%AE.wav",
    },
    {
        .name = "都市沙漠",
        .artist = "Xy.李傲铭×Rc.Hush",
        .url = "http://lubancat.local:8081/music/Xy.%E6%9D%8E%E5%82%B2%E9%93%AD%C3%97Rc.Hush-%E9%83%BD%E5%B8%82%E6%B2%99%E6%BC%A0.wav",
    },
    {
        .name = "七重人格",
        .artist = "Xy.酷酷的蓝L×R.魅",
        .url = "http://lubancat.local:8081/music/Xy.%E9%85%B7%E9%85%B7%E7%9A%84%E8%93%9DL%C3%97R.%E9%AD%85-%E4%B8%83%E9%87%8D%E4%BA%BA%E6%A0%BC.wav",
    },
    {
        .name = "几分之几",
        .artist = "Xy.阿火×Xy.阿豪×Xy.阿祖",
        .url = "http://lubancat.local:8081/music/Xy.%E9%98%BF%E7%81%AB%C3%97Xy.%E9%98%BF%E8%B1%AA%C3%97Xy.%E9%98%BF%E7%A5%96-%E5%87%A0%E5%88%86%E4%B9%8B%E5%87%A0.wav",
    },
    {
        .name = "那个女孩",
        .artist = "Xy.顾谦虚×Xy.水星",
        .url = "http://lubancat.local:8081/music/Xy.%E9%A1%BE%E8%B0%A6%E8%99%9A%C3%97Xy.%E6%B0%B4%E6%98%9F-%E9%82%A3%E4%B8%AA%E5%A5%B3%E5%AD%A9.wav",
    },
    {
        .name = "山歌王",
        .artist = "丶东锅锅×仙某某",
        .url = "http://lubancat.local:8081/music/%E4%B8%B6%E4%B8%9C%E9%94%85%E9%94%85%C3%97%E4%BB%99%E6%9F%90%E6%9F%90-%E5%B1%B1%E6%AD%8C%E7%8E%8B.mp3",
    },
    {
        .name = "怂 Shy",
        .artist = "丸儿×OrangebabyOAO",
        .url = "http://lubancat.local:8081/music/%E4%B8%B8%E5%84%BF%C3%97OrangebabyOAO-%E6%80%82%20Shy.wav",
    },
    {
        .name = "爱丽丝卿",
        .artist = "仙某某×娜子nAzi",
        .url = "http://lubancat.local:8081/music/%E4%BB%99%E6%9F%90%E6%9F%90%C3%97%E5%A8%9C%E5%AD%90nAzi-%E7%88%B1%E4%B8%BD%E4%B8%9D%E5%8D%BF.wav",
    },
    {
        .name = "不灵不灵",
        .artist = "倪莫问",
        .url = "http://lubancat.local:8081/music/%E5%80%AA%E8%8E%AB%E9%97%AE%20-%20%E4%B8%8D%E7%81%B5%E4%B8%8D%E7%81%B5.mp3",
    },
    {
        .name = "岁月里的花",
        .artist = "倪莫问×Running",
        .url = "http://lubancat.local:8081/music/%E5%80%AA%E8%8E%AB%E9%97%AE%C3%97Running-%E5%B2%81%E6%9C%88%E9%87%8C%E7%9A%84%E8%8A%B1.mp3",
    },
    {
        .name = "不可道",
        .artist = "大哥L×楼七七",
        .url = "http://lubancat.local:8081/music/%E5%A4%A7%E5%93%A5L%C3%97%E6%A5%BC%E4%B8%83%E4%B8%83-%E4%B8%8D%E5%8F%AF%E9%81%93.mp3",
    },
    {
        .name = "安可×图灵 底牌",
        .artist = "本地音乐",
        .url = "http://lubancat.local:8081/music/%E5%AE%89%E5%8F%AF%C3%97%E5%9B%BE%E7%81%B5%20%E5%BA%95%E7%89%8C.wav",
    },
    {
        .name = "如果可以",
        .artist = "小风9233×dl梦伴",
        .url = "http://lubancat.local:8081/music/%E5%B0%8F%E9%A3%8E9233%C3%97dl%E6%A2%A6%E4%BC%B4-%E5%A6%82%E6%9E%9C%E5%8F%AF%E4%BB%A5.wav",
    },
    {
        .name = "隔壁泰山",
        .artist = "开心市民甜粥×小盒子",
        .url = "http://lubancat.local:8081/music/%E5%BC%80%E5%BF%83%E5%B8%82%E6%B0%91%E7%94%9C%E7%B2%A5%C3%97%E5%B0%8F%E7%9B%92%E5%AD%90-%E9%9A%94%E5%A3%81%E6%B3%B0%E5%B1%B1.wav",
    },
    {
        .name = "霸王别姬",
        .artist = "憨憨小炮辉×南妹儿",
        .url = "http://lubancat.local:8081/music/%E6%86%A8%E6%86%A8%E5%B0%8F%E7%82%AE%E8%BE%89%C3%97%E5%8D%97%E5%A6%B9%E5%84%BF-%E9%9C%B8%E7%8E%8B%E5%88%AB%E5%A7%AC.wav",
    },
    {
        .name = "缘分一道桥",
        .artist = "楼七七×757户外",
        .url = "http://lubancat.local:8081/music/%E6%A5%BC%E4%B8%83%E4%B8%83%C3%97757%E6%88%B7%E5%A4%96-%E7%BC%98%E5%88%86%E4%B8%80%E9%81%93%E6%A1%A5.wav",
    },
    {
        .name = "雨蝶",
        .artist = "楼七七×发发",
        .url = "http://lubancat.local:8081/music/%E6%A5%BC%E4%B8%83%E4%B8%83%C3%97%E5%8F%91%E5%8F%91-%E9%9B%A8%E8%9D%B6.wav",
    },
    {
        .name = "慢慢",
        .artist = "王给给×姚远",
        .url = "http://lubancat.local:8081/music/%E7%8E%8B%E7%BB%99%E7%BB%99%C3%97%E5%A7%9A%E8%BF%9C-%E6%85%A2%E6%85%A2.wav",
    },
    {
        .name = "夏洛特烦恼",
        .artist = "珑·斯琴MIA×俊男",
        .url = "http://lubancat.local:8081/music/%E7%8F%91%C2%B7%E6%96%AF%E7%90%B4MIA%C3%97%E4%BF%8A%E7%94%B7-%E5%A4%8F%E6%B4%9B%E7%89%B9%E7%83%A6%E6%81%BC.wav",
    },
    {
        .name = "讨厌",
        .artist = "芮恩",
        .url = "http://lubancat.local:8081/music/%E8%8A%AE%E6%81%A9%20-%20%E8%AE%A8%E5%8E%8C.mp3",
    },
    {
        .name = "垃圾桶",
        .artist = "袁一琦",
        .url = "http://lubancat.local:8081/music/%E8%A2%81%E4%B8%80%E7%90%A6%20-%20%E5%9E%83%E5%9C%BE%E6%A1%B6.mp3",
    },
    {
        .name = "春娇与志明",
        .artist = "闲乘月×暗黑大米.m",
        .url = "http://lubancat.local:8081/music/%E9%97%B2%E4%B9%98%E6%9C%88%C3%97%E6%9A%97%E9%BB%91%E5%A4%A7%E7%B1%B3.m-%E6%98%A5%E5%A8%87%E4%B8%8E%E5%BF%97%E6%98%8E.wav",
    },
    {
        .name = "Golden",
        .artist = "阿福×若若跑的贼快",
        .url = "http://lubancat.local:8081/music/%E9%98%BF%E7%A6%8F%C3%97%E8%8B%A5%E8%8B%A5%E8%B7%91%E7%9A%84%E8%B4%BC%E5%BF%AB-Golden.mp3",
    },
    {
        .name = "我们的明天",
        .artist = "韩饱饱×阿yuan×居居侠ov0",
        .url = "http://lubancat.local:8081/music/%E9%9F%A9%E9%A5%B1%E9%A5%B1%C3%97%E9%98%BFyuan%C3%97%E5%B1%85%E5%B1%85%E4%BE%A0ov0-%E6%88%91%E4%BB%AC%E7%9A%84%E6%98%8E%E5%A4%A9.wav",
    },
    {
        .name = "长夜",
        .artist = "颜王词秀×程大牙",
        .url = "http://lubancat.local:8081/music/%E9%A2%9C%E7%8E%8B%E8%AF%8D%E7%A7%80%C3%97%E7%A8%8B%E5%A4%A7%E7%89%99-%E9%95%BF%E5%A4%9C.mp3",
    },
};

size_t ai_music_library_count(void)
{
    return sizeof(s_tracks) / sizeof(s_tracks[0]);
}

const ai_music_track_t *ai_music_library_get(size_t index)
{
    if (index >= ai_music_library_count()) {
        return NULL;
    }
    return &s_tracks[index];
}

static bool ai_music_library_is_alias_separator(char ch)
{
    return ch == ' ' || ch == '-' || ch == '_' || ch == '/';
}

static bool ai_music_library_query_contains_token(const char *query,
                                                  const char *token,
                                                  size_t token_len)
{
    if (query == NULL || token == NULL || token_len < 3) {
        return false;
    }

    for (const char *cursor = query; *cursor != '\0'; ++cursor) {
        if (strncmp(cursor, token, token_len) == 0) {
            return true;
        }
    }
    return false;
}

static bool ai_music_library_track_matches(const ai_music_track_t *track,
                                           const char *query)
{
    if (track == NULL || query == NULL) {
        return false;
    }

    if (strstr(track->name, query) != NULL ||
        strstr(track->artist, query) != NULL ||
        strstr(query, track->name) != NULL) {
        return true;
    }

    const char *token_start = track->name;
    for (const char *cursor = track->name; ; ++cursor) {
        if (*cursor == '\0' || ai_music_library_is_alias_separator(*cursor)) {
            size_t token_len = (size_t)(cursor - token_start);
            if (ai_music_library_query_contains_token(query, token_start, token_len)) {
                return true;
            }
            if (*cursor == '\0') {
                break;
            }
            token_start = cursor + 1;
        }
    }
    return false;
}

const ai_music_track_t *ai_music_library_find(const char *query)
{
    if (query == NULL || query[0] == '\0') {
        return NULL;
    }
    for (size_t i = 0; i < ai_music_library_count(); ++i) {
        if (ai_music_library_track_matches(&s_tracks[i], query)) {
            return &s_tracks[i];
        }
    }
    return NULL;
}

esp_err_t ai_music_library_format_list(char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    int written = snprintf(buffer, buffer_size, "本地歌单：");
    if (written < 0 || (size_t)written >= buffer_size) {
        return ESP_ERR_NO_MEM;
    }

    size_t used = (size_t)written;
    for (size_t i = 0; i < ai_music_library_count(); ++i) {
        written = snprintf(buffer + used,
                           buffer_size - used,
                           "%s《%s》",
                           i == 0 ? "" : "、",
                           s_tracks[i].name);
        if (written < 0 || (size_t)written >= buffer_size - used) {
            return ESP_ERR_NO_MEM;
        }
        used += (size_t)written;
    }
    return ESP_OK;
}
