/* XMRigCC
 * Copyright 2017-     BenDr0id    <https://github.com/BenDr0id>, <ben@graef.in>
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
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
#include <sstream>
#include <fstream>
#include "3rdparty/rapidjson/stringbuffer.h"
#include "3rdparty/rapidjson/prettywriter.h"
#include <crypto/common/VirtualMemory.h>
#include "base/kernel/Env.h"
#include "base/io/log/Tags.h"

#include "backend/cpu/Cpu.h"
#include "base/tools/Timer.h"
#include "base/tools/Chrono.h"
#include "base/kernel/Base.h"
#include "base/kernel/Platform.h"

#include "base/cc/interfaces/IClientStatusListener.h"
#include "base/cc/interfaces/ICommandListener.h"

#include "CCClient.h"
#include "App.h"
#include "version.h"

#ifdef TYPE_AMD_GPU
#include "common/log/Log.h"
#include "common/log/RemoteLog.h"
#include "common/Platform.h"
#include "core/Config.h"
#else

#include "core/config/Config.h"
#include "base/io/log/Log.h"
#include "base/io/log/backends/RemoteLog.h"

#endif

#if _WIN32
#   include "winsock2.h"
#else

#   include "unistd.h"

#endif

namespace
{
  static std::string VersionString()
  {
    std::string version = std::to_string(APP_VER_MAJOR) + std::string(".") + std::to_string(APP_VER_MINOR) +
                          std::string(".") + std::to_string(APP_VER_PATCH);
    return version;
  }
}

#ifdef TYPE_AMD_GPU
xmrig::CCClient::CCClient(xmrig::Config* config, uv_async_t* async)
#else

xmrig::CCClient::CCClient(Base* base)
#endif
  : m_base(base),
    m_startTime(Chrono::currentMSecsSinceEpoch()),
    m_configPublishedOnStart(false),
    m_timer(nullptr)
{
  base->addListener(this);

  m_timer = new Timer(this);
}


xmrig::CCClient::~CCClient()
{
  LOG_DEBUG("CCClient::~CCCLient()");
  delete m_timer;
}

void xmrig::CCClient::start()
{
  LOG_DEBUG("CCClient::start");

  updateAuthorization();
  updateClientInfo();

  m_timer->start(static_cast<uint64_t>(m_base->config()->ccClient().updateInterval() * 1000),
                 static_cast<uint64_t>(m_base->config()->ccClient().updateInterval() * 1000));
}

void xmrig::CCClient::updateAuthorization()
{
  LOG_DEBUG("CCClient::updateAuthorization");

  if (m_base->config()->ccClient().token() != nullptr)
  {
    m_authorization = std::string("Bearer ") + m_base->config()->ccClient().token();
  }
}

void xmrig::CCClient::updateClientInfo()
{
  LOG_DEBUG("CCClient::updateClientInfo");

  std::string clientId;
  if (m_base->config()->ccClient().workerId())
  {
    clientId = Env::expand(m_base->config()->ccClient().workerId());
  }
  else
  {
    clientId = Env::hostname();
  }

  auto cpuInfo = xmrig::Cpu::info();

  m_clientStatus.setClientId(clientId);
  m_clientStatus.setVersion(VersionString());
  m_clientStatus.setCpuBrand(cpuInfo->brand());
  m_clientStatus.setCpuAES(cpuInfo->hasAES());
  m_clientStatus.setCpuSockets(static_cast<int>(cpuInfo->packages()));
  m_clientStatus.setCpuCores(static_cast<int>(cpuInfo->cores()));
  m_clientStatus.setCpuThreads(static_cast<int>(cpuInfo->threads()));
  m_clientStatus.setCpuX64(ICpuInfo::is64bit());
  m_clientStatus.setVM(cpuInfo->isVM());
  m_clientStatus.setCpuL2(static_cast<int>(cpuInfo->L2() / 1024));
  m_clientStatus.setCpuL3(static_cast<int>(cpuInfo->L3() / 1024));
  m_clientStatus.setNodes(static_cast<int>(cpuInfo->nodes()));

#   ifdef XMRIG_FEATURE_ASM
  const xmrig::Assembly assembly = Cpu::assembly(cpuInfo->assembly());
  m_clientStatus.setAssembly(assembly.toString());
#   else
  m_clientStatus.setAssembly("none");
#   endif


#ifdef TYPE_AMD_GPU
  m_clientStatus.setCurrentThreads(static_cast<int>(config->threads().size()));
  m_clientStatus.setCurrentAlgoName(config->algorithm().name());
#endif
}


void xmrig::CCClient::stop()
{
  LOG_DEBUG("CCClient::stop");

  m_configPublishedOnStart = false;

  if (m_timer)
  {
    m_timer->stop();
  }

  if (m_thread.joinable())
  {
    m_thread.join();
  }
}

void xmrig::CCClient::updateStatistics()
{
  LOG_DEBUG("CCClient::updateStatistics");

  for (IClientStatusListener* listener : m_ClientStatuslisteners)
  {
    listener->onUpdateRequest(m_clientStatus);
  }

#ifdef TYPE_AMD_GPU
  m_self->m_clientStatus.setHashFactor(0);
  m_self->m_clientStatus.setHugepagesEnabled(false);
  m_self->m_clientStatus.setHugepages(false);
  m_self->m_clientStatus.setTotalPages(0);
  m_self->m_clientStatus.setTotalHugepages(0);
  m_self->m_clientStatus.setCurrentPowVariantName(xmrig::Algorithm::getVariantName(network.powVariant));
#endif

}

#ifdef TYPE_AMD_GPU
void CCClient::updateGpuInfo(const std::vector<GpuContext>& gpuContext)
{
  LOG_DEBUG("CCClient::updateGpuInfo");

    if (m_self) {
        uv_mutex_lock(&m_mutex);

        m_self->m_clientStatus.clearGPUInfoList();

        for (auto gpu : gpuContext) {
            GPUInfo gpuInfo;
            gpuInfo.setName(gpu.name);
            gpuInfo.setCompMode(gpu.compMode);
            gpuInfo.setComputeUnits(gpu.computeUnits);
            gpuInfo.setDeviceIdx(gpu.deviceIdx);
            gpuInfo.setFreeMem(gpu.freeMem);
            gpuInfo.setWorkSize(gpu.workSize);
            gpuInfo.setMaxWorkSize(gpu.maximumWorkSize);
            gpuInfo.setMemChunk(gpu.memChunk);
            gpuInfo.setRawIntensity(gpu.rawIntensity);

            m_self->m_clientStatus.addGPUInfo(gpuInfo);
        }

        uv_mutex_unlock(&m_mutex);
    }
}
#endif

void xmrig::CCClient::publishClientStatusReport()
{
  LOG_DEBUG("CCClient::publishClientStatusReport");

  std::string requestUrl = "/client/setClientStatus?clientId=" + m_clientStatus.getClientId();
  std::string requestBuffer = m_clientStatus.toJsonString();

  auto config = m_base->config()->ccClient();

  auto res = performRequest(requestUrl, requestBuffer, "POST");
  if (!res)
  {
    LOG_ERR(CLEAR "%s" RED("error:unable to performRequest POST -> http%s://%s:%d%s"), Tags::cc(),
            config.useTLS() ? "s":"", config.host(), config.port(), requestUrl.c_str());
  }
  else if (res->status != 200)
  {
    LOG_ERR(CLEAR "%s" RED("error:\"%d\" -> http%s://%s:%d%s"), Tags::cc(), res->status,
            config.useTLS() ? "s" : "", config.host(), config.port(), requestUrl.c_str());
  }
  else
  {
    LOG_DEBUG("CCClient::publishClientStatusReport received: '%s'", res->body.c_str());

    ControlCommand controlCommand;
    if (controlCommand.parseFromJsonString(res->body))
    {
      if (controlCommand.getCommand() == ControlCommand::START)
      {
        LOG_DEBUG(CLEAR "%s Command: START received -> resume", Tags::cc());
      }
      else if (controlCommand.getCommand() == ControlCommand::STOP)
      {
        LOG_DEBUG(CLEAR "%s Command: STOP received -> pause", Tags::cc());
      }
      else if (controlCommand.getCommand() == ControlCommand::UPDATE_CONFIG)
      {
        LOG_WARN(CLEAR "%s" YELLOW("Command: UPDATE_CONFIG received -> update config"), Tags::cc());
        fetchConfig();
      }
      else if (controlCommand.getCommand() == ControlCommand::PUBLISH_CONFIG)
      {
        LOG_WARN(CLEAR "%s" YELLOW("Command: PUBLISH_CONFIG received -> publish config"), Tags::cc());
        publishConfig();
      }
      else if (controlCommand.getCommand() == ControlCommand::RESTART)
      {
        LOG_WARN(CLEAR "%s" YELLOW("Command: RESTART received -> trigger restart"), Tags::cc());
      }
      else if (controlCommand.getCommand() == ControlCommand::SHUTDOWN)
      {
        LOG_WARN(CLEAR "%s" YELLOW("Command: SHUTDOWN received -> quit"), Tags::cc());
      }
      else if (controlCommand.getCommand() == ControlCommand::REBOOT)
      {
        LOG_WARN(CLEAR "%s" YELLOW("Command: REBOOT received -> trigger reboot"), Tags::cc());
      }
      else if (controlCommand.getCommand() == ControlCommand::EXECUTE)
      {
        LOG_WARN(CLEAR "%s" YELLOW("Command: EXECUTE received -> trigger execute"), Tags::cc());
      }

      for (ICommandListener *listener : m_Commandlisteners)
      {
        listener->onCommandReceived(controlCommand);
      }
    }
    else
    {
      LOG_ERR(CLEAR "%s" RED("Unknown command received from CC Server."), Tags::cc());
    }
  }
}

void xmrig::CCClient::fetchConfig()
{
  LOG_DEBUG("CCClient::fetchConfig");

  auto config = m_base->config()->ccClient();

  std::string requestUrl = "/client/getConfig?clientId=" + m_clientStatus.getClientId();
  std::string requestBuffer;

  auto res = performRequest(requestUrl, requestBuffer, "GET");
  if (!res)
  {
    LOG_ERR(CLEAR "%s" RED("error:unable to performRequest GET -> http%s://%s:%d%s"), Tags::cc(),
            config.useTLS() ? "s" : "", config.host(), config.port(), requestUrl.c_str());
  }
  else if (res->status != 200)
  {
    LOG_ERR(CLEAR "%s" RED("error:\"%d\" -> http%s://%s:%d%s"), Tags::cc(), res->status,
            config.useTLS() ? "s" : "", config.host(), config.port(), requestUrl.c_str());
  }
  else
  {
    LOG_DEBUG("CCClient::fetchConfig received: '%s'", res->body.c_str());

    rapidjson::Document document;
    if (!document.Parse(res->body.c_str()).HasParseError())
    {
      std::ofstream clientConfigFile(m_base->config()->fileName());
      if (clientConfigFile)
      {
        rapidjson::StringBuffer buffer(0, 65536);
        rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
        writer.SetMaxDecimalPlaces(10);
        document.Accept(writer);

        clientConfigFile << buffer.GetString();
        clientConfigFile.close();

        if (!m_base->config()->isWatch())
        {
          static_cast<IWatcherListener*>(m_base)->onFileChanged(m_base->config()->fileName());
        }

        LOG_WARN(CLEAR "%s" YELLOW("Config updated. -> reload"), Tags::cc());
      }
      else
      {
        LOG_ERR(CLEAR "%s" RED("Not able to store client config to file %s."), Tags::cc(), m_base->config()->fileName().data());
      }
    }
    else
    {
      LOG_ERR(CLEAR "%s" RED("Not able to store client config. received client config is broken!"), Tags::cc());
    }
  }
}

void xmrig::CCClient::publishConfig()
{
  LOG_DEBUG("CCClient::publishConfig");

  auto config = m_base->config()->ccClient();

  std::string requestUrl = "/client/setClientConfig?clientId=" + m_clientStatus.getClientId();

  std::stringstream data;
  std::ifstream clientConfig(m_base->config()->fileName());

  if (clientConfig)
  {
    data << clientConfig.rdbuf();
    clientConfig.close();
  }

  if (data.tellp() > 0)
  {
    rapidjson::Document document;
    document.Parse(data.str().c_str());

    if (!document.HasParseError())
    {
      rapidjson::StringBuffer buffer(0, 65536);
      rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
      writer.SetMaxDecimalPlaces(10);
      document.Accept(writer);

      auto res = performRequest(requestUrl, buffer.GetString(), "POST");
      if (!res)
      {
        LOG_ERR(CLEAR "%s" RED("error:unable to performRequest POST -> http%s://%s:%d%s"), Tags::cc(),
                config.useTLS() ? "s" : "", config.host(), config.port(), requestUrl.c_str());
      }
      else if (res->status != 200)
      {
        LOG_ERR(CLEAR "%s" RED("error:\"%d\" -> http%s://%s:%d%s"), Tags::cc(), res->status, config.host(),
                config.useTLS() ? "s" : "", config.port(), requestUrl.c_str());
      }
      else
      {
        LOG_DEBUG("CCClient::publishConfig received: '%s'", res->body.c_str());
      }
    }
    else
    {
      LOG_ERR(CLEAR "%s" RED("Not able to send config. Client config %s is broken!"), Tags::cc(), m_base->config()->fileName().data());
    }
  }
  else
  {
    LOG_ERR(CLEAR "%s" RED("Not able to load client config %s. Please make sure it exists! Using embedded config."), Tags::cc(),
            m_base->config()->fileName().data());
  }
}

std::shared_ptr<httplib::Response> xmrig::CCClient::performRequest(const std::string& requestUrl,
                                                                   const std::string& requestBuffer,
                                                                   const std::string& operation)
{
  std::shared_ptr<httplib::ClientImpl> cli;

  auto config = m_base->config()->ccClient();

#   ifdef XMRIG_FEATURE_TLS
  if (config.useTLS())
  {
    cli = std::make_shared<httplib::SSLClient>(config.host(),
                                               config.port());
    cli->enable_server_certificate_verification(false);
  }
  else
  {
#   endif
    cli = std::make_shared<httplib::ClientImpl>(config.host(),
                                                config.port());
#   ifdef XMRIG_FEATURE_TLS
  }
#   endif

  std::stringstream hostHeader;
  hostHeader << config.host()
             << ":"
             << config.port();

  LOG_DEBUG("CCClient::performRequest %s -> [%s%s] send: '%.2048s'", operation.c_str(), hostHeader.str().c_str(), requestUrl.c_str(), requestBuffer.c_str());

  httplib::Request req;
  req.method = operation;
  req.path = requestUrl;
  req.set_header("Host", hostHeader.str());
  req.set_header("Accept", "*//*");
  req.set_header("User-Agent", Platform::userAgent());
  req.set_header("Accept", "application/json");
  req.set_header("Content-Type", "application/json");

  if (!m_authorization.empty())
  {
    req.set_header("Authorization", m_authorization.c_str());
  }

  if (!requestBuffer.empty())
  {
    req.body = requestBuffer;
  }

  auto res = std::make_shared<httplib::Response>();

  httplib::Error err = httplib::Error::Success;

  return cli->send(req, *res, err) ? res : nullptr;
}

void xmrig::CCClient::updateUptime()
{
  LOG_DEBUG("CCClient::updateUptime");
  m_clientStatus.setUptime(Chrono::currentMSecsSinceEpoch() - m_startTime);
}

void xmrig::CCClient::updateLog()
{
  LOG_DEBUG("CCClient::updateLog");
  m_clientStatus.setLog(RemoteLog::getRows());
}

void xmrig::CCClient::onConfigChanged(Config* config, Config* previousConfig)
{
  LOG_DEBUG("CCClient::onConfigChanged");
  if (config->ccClient() != previousConfig->ccClient())
  {
    config->ccClient().print();

    stop();

    if (config->ccClient().enabled() && config->ccClient().host() && config->ccClient().port() > 0)
    {
      start();
    }
  }
}

void xmrig::CCClient::onTimer(const xmrig::Timer* timer)
{
  LOG_DEBUG("CCClient::onTimer");
  if (!m_thread.joinable())
  {
    m_thread = std::thread(&CCClient::publishThread, this);
    m_thread.detach();
  }
}

void xmrig::CCClient::publishThread()
{
  LOG_DEBUG("CCClient::publishThread()");
  if (!m_configPublishedOnStart && m_base->config()->ccClient().uploadConfigOnStartup())
  {
    m_configPublishedOnStart = true;
    publishConfig();
  }

  updateUptime();
  updateLog();
  updateStatistics();

  publishClientStatusReport();
}