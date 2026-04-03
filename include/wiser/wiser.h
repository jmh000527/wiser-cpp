/**
 * @file wiser.h
 * @brief Wiser 搜索引擎的主头文件。
 * 
 * 包含所有核心组件的头文件，方便用户统一引用。
 */

#pragma once

// Wiser C++ Search Engine
// Modern C++ rewrite of the wiser search engine

#include "wiser/types.h"
#include "wiser/config.h"
#include "wiser/config_loader.h"
#include "wiser/console.h"
#include "wiser/progress_bar.h"
#include "wiser/log_init.h"
#include "wiser/utils.h"
#include "wiser/postings.h"
#include "wiser/query_parser.h"
#include "wiser/synonym_dict.h"
#include "wiser/database.h"
#include "wiser/tokenizer.h"
#include "wiser/search_engine.h"
#include "wiser/wiki_loader.h"
#include "wiser/wiser_environment.h"
#include "wiser/tsv_loader.h"
#include "wiser/json_loader.h"
