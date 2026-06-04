/*!
 *  \file web_server_data.h
 *
 *  \brief
 *  Application Web Server File Data
 *
 *  \author
 *  Texas Instruments Incorporated
 *
 *  \copyright
 *  Copyright (C) 2025 Texas Instruments Incorporated
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *    Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 *    Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the
 *    distribution.
 *
 *    Neither the name of Texas Instruments Incorporated nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef GSP_WS_JS_H
#define GSP_WS_JS_H

#ifdef __cplusplus
extern "C" {
#endif

    static const char main_js[] =
    "function getCpuLoad()"
    "{"
        "var cpuLoad = 0;"
        "function getCpuLoadComplete()"
        "{"
            "if(cpuLoad.readyState == 4)"
            "{"
                "if(cpuLoad.status == 200)"
                "{"
                    "if(cpuLoad.responseText != null)"
                    "{"
                        "var rowsNum = 20;"
                        "var id = \"\";"
                        "var row = 0;"
                        "var data = cpuLoad.responseText;"
                        "const dataArray = data.split(\",\");"
                        "for (let i = 0; i < dataArray.length; i = i + 2)"
                        "{"
                            "row = i/2;"
                            "if (rowsNum > i/2)"
                            "{"
                                "id = \"r\" + row + \"c0\";"
                                "document.getElementById(id).innerHTML = dataArray[i];"
                                "id = \"r\" + row + \"c1\";"
                                "document.getElementById(id).innerHTML = dataArray[i + 1];"
                            "}"
                        "}"
                    "}"
                "}"
            "}"
        "}"
        "cpuLoad = new XMLHttpRequest();"
        "if(cpuLoad)"
        "{"
            "cpuLoad.open(\"GET\", \"/cpuLoad?id=\" + Math.random(), true);"
            "cpuLoad.responseType = \"text\";"
            "cpuLoad.onreadystatechange = getCpuLoadComplete;"
            "cpuLoad.send(null);"
        "}"
        "setTimeout('getCpuLoad()', 1000);"
    "}";

    static const char wsio_js[] =
    "function get_IOdata()"
    "{"
    "    var ioReq = new XMLHttpRequest();"

    "    ioReq.onreadystatechange = function()"
    "    {"
    "        if(ioReq.readyState == 4)"
    "        {"
    "            if(ioReq.status == 200)"
    "            {"
    "                if(ioReq.responseText != null)"
    "                {"
    "                    var container = document.getElementById('iotable_container');"
    "                    if(container)"
    "                    {"
    "                        container.innerHTML = ioReq.responseText;"
    "                    }"
    "                }"
    "            }"
    "        }"
    "    };"

    "    ioReq.open('GET', '/io-data?id=' + Math.random(), true);"
    "    ioReq.send(null);"

    "    setTimeout(get_IOdata, 50);"
    "}";

#ifdef  __cplusplus
}
#endif

#endif // GSP_WS_JS_H
