/* XMRig
 * Copyright 2010      Jeff Garzik <jgarzik@pobox.com>
 * Copyright 2012-2014 pooler      <pooler@litecoinpool.org>
 * Copyright 2014      Lucas Jones <https://github.com/lucasjones>
 * Copyright 2014-2016 Wolf9466    <https://github.com/OhGodAPet>
 * Copyright 2016      Jay D Dee   <jayddee246@gmail.com>
 * Copyright 2016-2017 XMRig       <support@xmrig.com>
 *
 *
 *   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <cstring>
#include <3rdparty/rapidjson/document.h>
#include "server/Service.h"
#include "log/Log.h"

uv_mutex_t Service::m_mutex;


bool Service::start()
{
    uv_mutex_init(&m_mutex);

    return true;
}


void Service::release()
{
    uv_mutex_lock(&m_mutex);

    m_clientStatus.clear();
    m_clientCommand.clear();

    uv_mutex_unlock(&m_mutex);
}

unsigned Service::get(const std::string &url, std::string &resp)
{
    uv_mutex_lock(&m_mutex);

    LOG_INFO("GET(%s)", url.c_str());

    uv_mutex_unlock(&m_mutex);

    return 200;
}

unsigned Service::post(const std::string &url, const std::string &data, std::string &resp)
{
    uv_mutex_lock(&m_mutex);

    LOG_INFO("POST(url='%s', data='%s')", url.c_str(), data.c_str());

    rapidjson::Document document;
    if (!document.Parse(data.c_str()).HasParseError()) {
        LOG_INFO("Status from miner: %s", document["miner"].GetString());
    } else {
        LOG_ERR("Parse Error Occured: %d", document.GetParseError());
        return MHD_HTTP_BAD_REQUEST;
    }

    ControlCommand controlCommand;
    resp = controlCommand.toJson();


    uv_mutex_unlock(&m_mutex);

    return 200;
}