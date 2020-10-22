/*
 * Copyright (c) 2019-2020, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define LOG_TAG "PAL: SessionAlsaCompress"

#include "SessionAlsaCompress.h"
#include "SessionAlsaUtils.h"
#include "Stream.h"
#include "ResourceManager.h"
#include "media_fmt_api.h"
#include "gapless_api.h"
#include <agm_api.h>
#include <sstream>
#include <mutex>
#include <fstream>

void SessionAlsaCompress::getSndCodecParam(struct snd_codec &codec, struct pal_stream_attributes &sAttr)
{
    struct pal_media_config *config = &sAttr.out_media_config;

    codec.id = getSndCodecId(config->aud_fmt_id);
    codec.ch_in = config->ch_info.channels;
    codec.ch_out = codec.ch_in;
    codec.sample_rate = config->sample_rate;
    codec.bit_rate = config->bit_width;
}

int SessionAlsaCompress::getSndCodecId(pal_audio_fmt_t fmt)
{
    int id = -1;

    switch (fmt) {
        case PAL_AUDIO_FMT_MP3:
            id = SND_AUDIOCODEC_MP3;
            break;
#ifdef SND_COMPRESS_DEC_HDR
        case PAL_AUDIO_FMT_COMPRESSED_RANGE_BEGIN:
        case PAL_AUDIO_FMT_COMPRESSED_EXTENDED_RANGE_BEGIN:
        case PAL_AUDIO_FMT_COMPRESSED_EXTENDED_RANGE_END:
            id = -1;
            break;
        case PAL_AUDIO_FMT_AAC:
        case PAL_AUDIO_FMT_AAC_ADTS:
        case PAL_AUDIO_FMT_AAC_ADIF:
        case PAL_AUDIO_FMT_AAC_LATM:
            id = SND_AUDIOCODEC_AAC;
            break;
        case PAL_AUDIO_FMT_WMA_STD:
            id = SND_AUDIOCODEC_WMA;
            break;
        case PAL_AUDIO_FMT_DEFAULT_PCM:
            id = SND_AUDIOCODEC_PCM;
            break;
        case PAL_AUDIO_FMT_ALAC:
            id = SND_AUDIOCODEC_ALAC;
            break;
        case PAL_AUDIO_FMT_APE:
            id = SND_AUDIOCODEC_APE;
            break;
        case PAL_AUDIO_FMT_WMA_PRO:
            id = SND_AUDIOCODEC_WMA_PRO;
            break;
        case PAL_AUDIO_FMT_FLAC:
        case PAL_AUDIO_FMT_FLAC_OGG:
            id = SND_AUDIOCODEC_FLAC;
            break;
        case PAL_AUDIO_FMT_VORBIS:
            id = SND_AUDIOCODEC_VORBIS;
            break;
#endif
        default:
            PAL_ERR(LOG_TAG, "Entered default format %x", fmt);
            break;
    }

    return id;
}

bool SessionAlsaCompress::isGaplessFormat(pal_audio_fmt_t fmt)
{
    bool isSupported = false;

    /* If platform doesn't support Gapless,
     * then return false for all formats.
     */
    if (!(rm->isGaplessEnabled)) {
        return isSupported;
    }
    switch (fmt) {
        case PAL_AUDIO_FMT_DEFAULT_COMPRESSED:
        case PAL_AUDIO_FMT_AAC:
        case PAL_AUDIO_FMT_AAC_ADTS:
        case PAL_AUDIO_FMT_AAC_ADIF:
        case PAL_AUDIO_FMT_AAC_LATM:
            isSupported = true;
            break;
        case PAL_AUDIO_FMT_WMA_STD:
            break;
        case PAL_AUDIO_FMT_DEFAULT_PCM:
            break;
        case PAL_AUDIO_FMT_ALAC:
            break;
        case PAL_AUDIO_FMT_APE:
            break;
        case PAL_AUDIO_FMT_WMA_PRO:
            break;
        case PAL_AUDIO_FMT_FLAC:
        case PAL_AUDIO_FMT_FLAC_OGG:
            break;
        case PAL_AUDIO_FMT_VORBIS:
            break;
        default:
            break;
    }
    PAL_DBG(LOG_TAG, "format %x, gapless supported %d", audio_fmt,
                      isSupported);
    return isSupported;
}

int SessionAlsaCompress::setCustomFormatParam(pal_audio_fmt_t audio_fmt)
{
    int32_t status = 0;
    uint8_t* payload = NULL;
    size_t payloadSize = 0;
    uint32_t miid;

    if (audio_fmt == PAL_AUDIO_FMT_VORBIS) {
        payload_media_fmt_vorbis_t* media_fmt_vorbis = NULL;
        // set config for vorbis, as it cannot be upstreamed.
        status = SessionAlsaUtils::getModuleInstanceId(mixer,
                    compressDevIds.at(0), rxAifBackEnds[0].second.data(),
                    STREAM_INPUT_MEDIA_FORMAT, &miid);
        if (0 != status) {
            PAL_ERR(LOG_TAG, "getModuleInstanceId failed");
            return status;
        }
        struct media_format_t *media_fmt_hdr = nullptr;
        media_fmt_hdr = (struct media_format_t *)
                            malloc(sizeof(struct media_format_t)
                                + sizeof(struct pal_snd_dec_vorbis));
        if (!media_fmt_hdr) {
            PAL_ERR(LOG_TAG, "failed to allocate memory");
            return -ENOMEM;
        }
        media_fmt_hdr->data_format = DATA_FORMAT_RAW_COMPRESSED ;
        media_fmt_hdr->fmt_id = MEDIA_FMT_ID_VORBIS;
        media_fmt_hdr->payload_size = sizeof(struct pal_snd_dec_vorbis);
        media_fmt_vorbis = (payload_media_fmt_vorbis_t*)(((uint8_t*)media_fmt_hdr) +
            sizeof(struct media_format_t));

        ar_mem_cpy(media_fmt_vorbis,
                            sizeof(struct pal_snd_dec_vorbis),
                            &codec.format,
                            sizeof(struct pal_snd_dec_vorbis));
        status = builder->payloadCustomParam(&payload, &payloadSize,
                                        (uint32_t *)media_fmt_hdr,
                                        sizeof(struct media_format_t) +
                                        sizeof(struct pal_snd_dec_vorbis),
                                        miid, PARAM_ID_MEDIA_FORMAT);
        free(media_fmt_hdr);
        if (status) {
            PAL_ERR(LOG_TAG,"payloadCustomParam failed status = %d", status);
            return status;
        }
        status = SessionAlsaUtils::setMixerParameter(mixer,
                        compressDevIds.at(0), payload, payloadSize);
        free(payload);
        if (status != 0) {
            PAL_ERR(LOG_TAG,"setMixerParameter failed");
            return status;
        }
    }

    return status;
}

void SessionAlsaCompress::offloadThreadLoop(SessionAlsaCompress* compressObj)
{
    std::shared_ptr<offload_msg> msg;
    uint32_t event_id;
    int ret = 0;
    bool is_drain_called = false;
    std::unique_lock<std::mutex> lock(compressObj->cv_mutex_);

    while (1) {
        if (compressObj->msg_queue_.empty())
            compressObj->cv_.wait(lock);  /* wait for incoming requests */

        if (!compressObj->msg_queue_.empty()) {
            msg = compressObj->msg_queue_.front();
            compressObj->msg_queue_.pop();
            lock.unlock();

            if (msg->cmd == OFFLOAD_CMD_EXIT)
                break; // exit the thread

            if (msg->cmd == OFFLOAD_CMD_WAIT_FOR_BUFFER) {
                if (compressObj->rm->cardState == CARD_STATUS_ONLINE) {
                    PAL_VERBOSE(LOG_TAG, "calling compress_wait");
                    ret = compress_wait(compressObj->compress, -1);
                    PAL_VERBOSE(LOG_TAG, "out of compress_wait, ret %d", ret);
                    event_id = PAL_STREAM_CBK_EVENT_WRITE_READY;
                }
            } else if (msg->cmd == OFFLOAD_CMD_DRAIN) {
                if (!is_drain_called && compressObj->playback_started) {
                    PAL_INFO(LOG_TAG, "calling compress_drain");
                    if (compressObj->rm->cardState == CARD_STATUS_ONLINE) {
                         ret = compress_drain(compressObj->compress);
                         PAL_INFO(LOG_TAG, "out of compress_drain, ret %d", ret);
                    }
                }
                if (ret == -ENETRESET) {
                    PAL_ERR(LOG_TAG, "Block drain ready event during SSR");
                    lock.lock();
                    continue;
                }
                is_drain_called = false;
                event_id = PAL_STREAM_CBK_EVENT_DRAIN_READY;
            } else if (msg->cmd == OFFLOAD_CMD_PARTIAL_DRAIN) {
                if (compressObj->playback_started) {
                    if (compressObj->rm->cardState == CARD_STATUS_ONLINE) {
                        if (compressObj->isGaplessFmt) {
                            PAL_DBG(LOG_TAG, "calling partial compress_drain");
                            ret = compress_next_track(compressObj->compress);
                            PAL_INFO(LOG_TAG, "out of compress next track, ret %d", ret);
                            if (ret == 0) {
                                ret = compress_partial_drain(compressObj->compress);
                                PAL_INFO(LOG_TAG, "out of partial compress_drain, ret %d", ret);
                            }
                            event_id = PAL_STREAM_CBK_EVENT_PARTIAL_DRAIN_READY;
                        } else {
                            PAL_DBG(LOG_TAG, "calling compress_drain");
                            ret = compress_drain(compressObj->compress);
                            PAL_INFO(LOG_TAG, "out of compress_drain, ret %d", ret);
                            is_drain_called = true;
                            event_id = PAL_STREAM_CBK_EVENT_DRAIN_READY;
                        }
                    }
                } else {
                    PAL_ERR(LOG_TAG, "Playback not started yet");
                    event_id = PAL_STREAM_CBK_EVENT_DRAIN_READY;
                    is_drain_called = true;
                }
                if (ret == -ENETRESET) {
                    PAL_ERR(LOG_TAG, "Block drain ready event during SSR");
                    lock.lock();
                    continue;
                }
            }  else if (msg->cmd == OFFLOAD_CMD_ERROR) {
                PAL_ERR(LOG_TAG, "Sending error to PAL client");
                event_id = PAL_STREAM_CBK_EVENT_ERROR;
            }
            if (compressObj->sessionCb)
                compressObj->sessionCb(compressObj->cbCookie, event_id, NULL, 0);

            lock.lock();
        }

    }
    PAL_DBG(LOG_TAG, "exit offloadThreadLoop");
}

SessionAlsaCompress::SessionAlsaCompress(std::shared_ptr<ResourceManager> Rm)
{
    rm = Rm;
    builder = new PayloadBuilder();

    /** set default snd codec params */
    codec.id = getSndCodecId(PAL_AUDIO_FMT_DEFAULT_PCM);
    codec.ch_in = 2;
    codec.ch_out = codec.ch_in;
    codec.sample_rate = 48000;
    codec.bit_rate = 16;
    customPayload = NULL;
    customPayloadSize = 0;
    compress = NULL;
    sessionCb = NULL;
    this->cbCookie = NULL;
    playback_started = false;
    playback_paused = false;
}

SessionAlsaCompress::~SessionAlsaCompress()
{
    delete builder;
}

int SessionAlsaCompress::open(Stream * s)
{
    int status = -EINVAL;
    struct pal_stream_attributes sAttr;
    std::vector<std::shared_ptr<Device>> associatedDevices;
    std::vector<std::pair<int32_t, std::string>> emptyBackEnds;

    status = s->getStreamAttributes(&sAttr);
    if (0 != status) {
        PAL_ERR(LOG_TAG,"getStreamAttributes Failed \n");
        return status;
    }

    ioMode = sAttr.flags & PAL_STREAM_FLAG_NON_BLOCKING_MASK;
    if (!ioMode) {
        PAL_ERR(LOG_TAG, "IO mode 0x%x not supported", ioMode);
        return -EINVAL;
    }
    status = s->getAssociatedDevices(associatedDevices);
    if (0 != status) {
        PAL_ERR(LOG_TAG,"getAssociatedDevices Failed \n");
        return status;
    }

    compressDevIds = rm->allocateFrontEndIds(sAttr, 0);
    if (compressDevIds.size() == 0) {
        PAL_ERR(LOG_TAG, "no more FE vailable");
        return -EINVAL;
    }
    for (int i = 0; i < compressDevIds.size(); i++) {
        //compressDevIds[i] = 5;
        PAL_DBG(LOG_TAG, "devid size %zu, compressDevIds[%d] %d", compressDevIds.size(), i, compressDevIds[i]);
    }
    rm->getBackEndNames(associatedDevices, rxAifBackEnds, emptyBackEnds);
    status = rm->getAudioMixer(&mixer);
    if (status) {
        PAL_ERR(LOG_TAG,"mixer error");
        return status;
    }
    status = SessionAlsaUtils::open(s, rm, compressDevIds, rxAifBackEnds);
    if (status) {
        PAL_ERR(LOG_TAG, "session alsa open failed with %d", status);
        rm->freeFrontEndIds(compressDevIds, sAttr, 0);
    }
    audio_fmt = sAttr.out_media_config.aud_fmt_id;
    isGaplessFmt = isGaplessFormat(audio_fmt);
    return status;
}

int SessionAlsaCompress::disconnectSessionDevice(Stream* streamHandle, pal_stream_type_t streamType,
        std::shared_ptr<Device> deviceToDisconnect)
{
    std::vector<std::shared_ptr<Device>> deviceList;
    struct pal_device dAttr;
    std::vector<std::pair<int32_t, std::string>> rxAifBackEndsToDisconnect;
    std::vector<std::pair<int32_t, std::string>> txAifBackEndsToDisconnect;
    int32_t status = 0;

    deviceList.push_back(deviceToDisconnect);
    rm->getBackEndNames(deviceList, rxAifBackEndsToDisconnect,
            txAifBackEndsToDisconnect);
    deviceToDisconnect->getDeviceAttributes(&dAttr);

    if (!rxAifBackEndsToDisconnect.empty()) {
        int cnt = 0;

        status = SessionAlsaUtils::disconnectSessionDevice(streamHandle, streamType, rm,
            dAttr, compressDevIds, rxAifBackEndsToDisconnect);

        for (const auto &elem : rxAifBackEnds) {
            cnt++;
            for (const auto &disConnectElem : rxAifBackEndsToDisconnect) {
                if (std::get<0>(elem) == std::get<0>(disConnectElem))
                    rxAifBackEnds.erase(rxAifBackEnds.begin() + cnt - 1, rxAifBackEnds.begin() + cnt);
            }
        }
    }

    if (!txAifBackEndsToDisconnect.empty()) {
        int cnt = 0;

        status = SessionAlsaUtils::disconnectSessionDevice(streamHandle, streamType, rm,
            dAttr, compressDevIds, txAifBackEndsToDisconnect);

        for (const auto &elem : txAifBackEnds) {
            cnt++;
            for (const auto &disConnectElem : txAifBackEndsToDisconnect) {
                if (std::get<0>(elem) == std::get<0>(disConnectElem))
                    txAifBackEnds.erase(txAifBackEnds.begin() + cnt - 1, txAifBackEnds.begin() + cnt);
            }
        }
    }

    return status;
}

int SessionAlsaCompress::setupSessionDevice(Stream* streamHandle, pal_stream_type_t streamType,
        std::shared_ptr<Device> deviceToConnect)
{
    std::vector<std::shared_ptr<Device>> deviceList;
    struct pal_device dAttr;
    std::vector<std::pair<int32_t, std::string>> rxAifBackEndsToConnect;
    std::vector<std::pair<int32_t, std::string>> txAifBackEndsToConnect;
    int32_t status = 0;

    deviceList.push_back(deviceToConnect);
    rm->getBackEndNames(deviceList, rxAifBackEndsToConnect,
            txAifBackEndsToConnect);
    deviceToConnect->getDeviceAttributes(&dAttr);

    if (!rxAifBackEndsToConnect.empty())
        status = SessionAlsaUtils::setupSessionDevice(streamHandle, streamType, rm,
            dAttr, compressDevIds, rxAifBackEndsToConnect);

    if (!txAifBackEndsToConnect.empty())
        status = SessionAlsaUtils::setupSessionDevice(streamHandle, streamType, rm,
            dAttr, compressDevIds, txAifBackEndsToConnect);

    return status;
}

int SessionAlsaCompress::connectSessionDevice(Stream* streamHandle, pal_stream_type_t streamType,
        std::shared_ptr<Device> deviceToConnect)
{
    std::vector<std::shared_ptr<Device>> deviceList;
    struct pal_device dAttr;
    std::vector<std::pair<int32_t, std::string>> rxAifBackEndsToConnect;
    std::vector<std::pair<int32_t, std::string>> txAifBackEndsToConnect;
    int32_t status = 0;

    deviceList.push_back(deviceToConnect);
    rm->getBackEndNames(deviceList, rxAifBackEndsToConnect,
            txAifBackEndsToConnect);
    deviceToConnect->getDeviceAttributes(&dAttr);

    if (!rxAifBackEndsToConnect.empty()) {
        status = SessionAlsaUtils::connectSessionDevice(NULL, streamHandle, streamType, rm,
            dAttr, compressDevIds, rxAifBackEndsToConnect);

        for (const auto &elem : rxAifBackEndsToConnect)
            rxAifBackEnds.push_back(elem);
    }

    if (!txAifBackEndsToConnect.empty()) {
        status = SessionAlsaUtils::connectSessionDevice(NULL, streamHandle, streamType, rm,
            dAttr, compressDevIds, txAifBackEndsToConnect);
        for (const auto &elem : txAifBackEndsToConnect)
            txAifBackEnds.push_back(elem);
    }

    return status;
}

int SessionAlsaCompress::prepare(Stream * s __unused)
{
   return 0;
}

int SessionAlsaCompress::setTKV(Stream * s __unused, configType type, effect_pal_payload_t *effectPayload)
{
    int status = 0;
    uint32_t tagsent;
    struct agm_tag_config* tagConfig = nullptr;
    const char *setParamTagControl = "setParamTag";
    const char *stream = "COMPRESS";
    struct mixer_ctl *ctl;
    std::ostringstream tagCntrlName;
    int tkv_size = 0;

    switch (type) {
        case MODULE:
        {
            pal_key_vector_t *pal_kvpair = (pal_key_vector_t *)effectPayload->payload;
            uint32_t num_tkvs = pal_kvpair->num_tkvs;
            for (uint32_t i = 0; i < num_tkvs; i++) {
                tkv.push_back(std::make_pair(pal_kvpair->kvp[i].key, pal_kvpair->kvp[i].value));
            }

            if (tkv.size() == 0) {
                status = -EINVAL;
                goto exit;
            }

            tagConfig = (struct agm_tag_config*)malloc (sizeof(struct agm_tag_config) +
                            (tkv.size() * sizeof(agm_key_value)));

            if (!tagConfig) {
                status = -ENOMEM;
                goto exit;
            }

            tagsent = effectPayload->tag;
            status = SessionAlsaUtils::getTagMetadata(tagsent, tkv, tagConfig);
            if (0 != status) {
                goto exit;
            }
            tagCntrlName<<stream<<compressDevIds.at(0)<<" "<<setParamTagControl;
            ctl = mixer_get_ctl_by_name(mixer, tagCntrlName.str().data());
            if (!ctl) {
                PAL_ERR(LOG_TAG, "Invalid mixer control: %s\n", tagCntrlName.str().data());
                status = -ENOENT;
                goto exit;
            }
            PAL_VERBOSE(LOG_TAG, "mixer control: %s\n", tagCntrlName.str().data());

            tkv_size = tkv.size()*sizeof(struct agm_key_value);
            status = mixer_ctl_set_array(ctl, tagConfig, sizeof(struct agm_tag_config) + tkv_size);
            if (status != 0) {
                PAL_ERR(LOG_TAG,"failed to set the tag calibration %d", status);
                goto exit;
            }
            ctl = NULL;
            tkv.clear();

            break;
        }
        default:
            PAL_ERR(LOG_TAG,"invalid type ");
            status = -EINVAL;
            goto exit;
    }

exit:
    PAL_DBG(LOG_TAG,"exit status:%d ", status);
    if (tagConfig) {
        free(tagConfig);
        tagConfig = nullptr;
    }

    return status;
}

int SessionAlsaCompress::setConfig(Stream * s, configType type, uint32_t tag1,
        uint32_t tag2, uint32_t tag3)
{
    int status = 0;
    uint32_t tagsent = 0;
    struct agm_tag_config* tagConfig = nullptr;
    std::ostringstream tagCntrlName;
    char const *stream = "COMPRESS";
    const char *setParamTagControl = "setParamTag";
    struct mixer_ctl *ctl = nullptr;
    uint32_t tkv_size = 0;

    switch (type) {
        case MODULE:
            tkv.clear();
            if (tag1)
                builder->populateTagKeyVector(s, tkv, tag1, &tagsent);
            if (tag2)
                builder->populateTagKeyVector(s, tkv, tag2, &tagsent);
            if (tag3)
                builder->populateTagKeyVector(s, tkv, tag3, &tagsent);

            if (tkv.size() == 0) {
                status = -EINVAL;
                goto exit;
            }
            tagConfig = (struct agm_tag_config*)malloc (sizeof(struct agm_tag_config) +
                            (tkv.size() * sizeof(agm_key_value)));
            if (!tagConfig) {
                status = -ENOMEM;
                goto exit;
            }
            status = SessionAlsaUtils::getTagMetadata(tagsent, tkv, tagConfig);
            if (0 != status) {
                goto exit;
            }
            tagCntrlName << stream << compressDevIds.at(0) << " " << setParamTagControl;
            ctl = mixer_get_ctl_by_name(mixer, tagCntrlName.str().data());
            if (!ctl) {
                PAL_ERR(LOG_TAG, "Invalid mixer control: %s\n", tagCntrlName.str().data());
                status = -ENOENT;
                goto exit;
            }

            tkv_size = tkv.size() * sizeof(struct agm_key_value);
            status = mixer_ctl_set_array(ctl, tagConfig, sizeof(struct agm_tag_config) + tkv_size);
            if (status != 0) {
                PAL_ERR(LOG_TAG,"failed to set the tag calibration %d", status);
                goto exit;
            }
            ctl = NULL;
            tkv.clear();
            break;
        default:
            status = 0;
            break;
    }

exit:
    if(tagConfig)
        free(tagConfig);
    return status;
}

int SessionAlsaCompress::setConfig(Stream * s, configType type, int tag)
{
    int status = 0;
    uint32_t tagsent;
    struct agm_tag_config* tagConfig;
    const char *setParamTagControl = "setParamTag";
    const char *stream = "COMPRESS";
    const char *setCalibrationControl = "setCalibration";
    struct mixer_ctl *ctl;
    struct agm_cal_config *calConfig;
    std::ostringstream tagCntrlName;
    std::ostringstream calCntrlName;
    int tkv_size = 0;
    int ckv_size = 0;

    switch (type) {
        case MODULE:
            status = builder->populateTagKeyVector(s, tkv, tag, &tagsent);
            if (0 != status) {
                PAL_ERR(LOG_TAG,"Failed to set the tag configuration\n");
                goto exit;
            }

            if (tkv.size() == 0) {
                status = -EINVAL;
                goto exit;
            }

            tagConfig = (struct agm_tag_config*)malloc (sizeof(struct agm_tag_config) +
                            (tkv.size() * sizeof(agm_key_value)));

            if (!tagConfig) {
                status = -ENOMEM;
                goto exit;
            }

            status = SessionAlsaUtils::getTagMetadata(tagsent, tkv, tagConfig);
            if (0 != status) {
                goto exit;
            }
            //TODO: how to get the id '5'
            tagCntrlName<<stream<<compressDevIds.at(0)<<" "<<setParamTagControl;
            ctl = mixer_get_ctl_by_name(mixer, tagCntrlName.str().data());
            if (!ctl) {
                PAL_ERR(LOG_TAG, "Invalid mixer control: %s\n", tagCntrlName.str().data());
                return -ENOENT;
            }
            PAL_VERBOSE(LOG_TAG, "mixer control: %s\n", tagCntrlName.str().data());

            tkv_size = tkv.size()*sizeof(struct agm_key_value);
            status = mixer_ctl_set_array(ctl, tagConfig, sizeof(struct agm_tag_config) + tkv_size);
            if (status != 0) {
                PAL_ERR(LOG_TAG,"failed to set the tag calibration %d", status);
            }
            ctl = NULL;
            if (tagConfig)
                free(tagConfig);
            tkv.clear();
            break;
            //todo calibration
        case CALIBRATION:
            status = builder->populateCalKeyVector(s, ckv, tag);
            if (0 != status) {
                PAL_ERR(LOG_TAG,"Failed to set the calibration data\n");
                goto exit;
            }
            if (ckv.size() == 0) {
                status = -EINVAL;
                goto exit;
            }

            calConfig = (struct agm_cal_config*)malloc (sizeof(struct agm_cal_config) +
                            (ckv.size() * sizeof(agm_key_value)));
            if (!calConfig) {
                status = -EINVAL;
                goto exit;
            }
            status = SessionAlsaUtils::getCalMetadata(ckv, calConfig);
            //TODO: how to get the id '0'
            calCntrlName<<stream<<compressDevIds.at(0)<<" "<<setCalibrationControl;
            ctl = mixer_get_ctl_by_name(mixer, calCntrlName.str().data());
            if (!ctl) {
                PAL_ERR(LOG_TAG, "Invalid mixer control: %s\n", calCntrlName.str().data());
                return -ENOENT;
            }
            PAL_VERBOSE(LOG_TAG, "mixer control: %s\n", calCntrlName.str().data());
            ckv_size = ckv.size()*sizeof(struct agm_key_value);
            //TODO make struct mixer and struct pcm as class private variables.
            status = mixer_ctl_set_array(ctl, calConfig, sizeof(struct agm_cal_config) + ckv_size);
            if (status != 0) {
                PAL_ERR(LOG_TAG,"failed to set the tag calibration %d", status);
            }
            ctl = NULL;
            if (calConfig)
                free(calConfig);
            ckv.clear();
            break;
        default:
            PAL_ERR(LOG_TAG,"invalid type ");
            status = -EINVAL;
            break;
    }

exit:
    PAL_DBG(LOG_TAG,"exit status:%d ", status);
    return status;
}

int SessionAlsaCompress::configureEarlyEOSDelay(void)
{
    int32_t status = 0;
    uint8_t* payload = NULL;
    size_t payloadSize = 0;
    uint32_t miid;

    PAL_DBG(LOG_TAG, "Enter");
    status = SessionAlsaUtils::getModuleInstanceId(mixer,
                    compressDevIds.at(0), rxAifBackEnds[0].second.data(),
                    MODULE_GAPLESS, &miid);
    if (0 != status) {
        PAL_ERR(LOG_TAG, "getModuleInstanceId failed");
        return status;
    }
    param_id_gapless_early_eos_delay_t  *early_eos_delay = nullptr;
    early_eos_delay = (struct param_id_gapless_early_eos_delay_t *)
                            malloc(sizeof(struct param_id_gapless_early_eos_delay_t));
    if (!early_eos_delay) {
        PAL_ERR(LOG_TAG, "failed to allocate memory");
        return -ENOMEM;
    }
    early_eos_delay->early_eos_delay_ms = EARLY_EOS_DELAY_MS;

    status = builder->payloadCustomParam(&payload, &payloadSize,
                                        (uint32_t *)early_eos_delay,
                                        sizeof(struct param_id_gapless_early_eos_delay_t),
                                        miid, PARAM_ID_EARLY_EOS_DELAY);
    free(early_eos_delay);
    if (status) {
        PAL_ERR(LOG_TAG,"payloadCustomParam failed status = %d", status);
        return status;
    }
    if (payloadSize) {
        status = updateCustomPayload(payload, payloadSize);
        delete payload;
        if(0 != status) {
            PAL_ERR(LOG_TAG,"%s: updateCustomPayload Failed\n", __func__);
            return status;
        }
    }
    return status;
}

int SessionAlsaCompress::start(Stream * s)
{
    struct compr_config compress_config;
    struct pal_stream_attributes sAttr;
    int32_t status = 0;
    size_t in_buf_size, in_buf_count, out_buf_size, out_buf_count;
    std::vector<std::shared_ptr<Device>> associatedDevices;
    struct pal_device dAttr;
    struct sessionToPayloadParam deviceData;
    uint8_t* payload = NULL;
    size_t payloadSize = 0;
    uint32_t miid;

    /** create an offload thread for posting callbacks */
    worker_thread = std::make_unique<std::thread>(offloadThreadLoop, this);

    s->getStreamAttributes(&sAttr);
    getSndCodecParam(codec, sAttr);
    s->getBufInfo(&in_buf_size,&in_buf_count,&out_buf_size,&out_buf_count);
    compress_config.fragment_size = out_buf_size;
    compress_config.fragments = out_buf_count;
    compress_config.codec = &codec;
    // compress_open
    compress = compress_open(rm->getSndCard(), compressDevIds.at(0), COMPRESS_IN, &compress_config);
    if (!compress) {
        PAL_ERR(LOG_TAG, "compress open failed");
        status = -EINVAL;
        goto free_feIds;
    }
    if (!is_compress_ready(compress)) {
        PAL_ERR(LOG_TAG, "compress open not ready %s", compress_get_error(compress));
        status = -EINVAL;
        goto free_feIds;
    }
    /** set non blocking mode for writes */
    compress_nonblock(compress, !!ioMode);

    switch (sAttr.direction) {
        case PAL_AUDIO_OUTPUT:
            status = s->getAssociatedDevices(associatedDevices);
            if (0 != status) {
                PAL_ERR(LOG_TAG,"getAssociatedDevices Failed \n");
                return status;
            }
            rm->getBackEndNames(associatedDevices, rxAifBackEnds, txAifBackEnds);
            if (rxAifBackEnds.empty() && txAifBackEnds.empty()) {
                PAL_ERR(LOG_TAG, "no backend specified for this stream");
                return status;

            }

            status = SessionAlsaUtils::getModuleInstanceId(mixer, (compressDevIds.at(0)),
                     rxAifBackEnds[0].second.data(), STREAM_SPR, &spr_miid);
            if (0 != status) {
                PAL_ERR(LOG_TAG, "Failed to get tag info %x, status = %d", STREAM_SPR, status);
                return status;
            }

            setCustomFormatParam(audio_fmt);
            for (int i = 0; i < associatedDevices.size();i++) {
                status = associatedDevices[i]->getDeviceAttributes(&dAttr);
                if(0 != status) {
                    PAL_ERR(LOG_TAG,"getAssociatedDevices Failed \n");
                    return status;
                }

                /* Get PSPD MFC MIID and configure to match to device config */
                /* This has to be done after sending all mixer controls and before connect */
                status = SessionAlsaUtils::getModuleInstanceId(mixer, compressDevIds.at(0),
                                                               rxAifBackEnds[i].second.data(),
                                                               TAG_DEVICE_MFC_SR, &miid);
                if (status != 0) {
                    PAL_ERR(LOG_TAG,"getModuleInstanceId failed");
                    return status;
                }
                PAL_DBG(LOG_TAG, "miid : %x id = %d, data %s, dev id = %d\n", miid,
                        compressDevIds.at(0), rxAifBackEnds[i].second.data(), dAttr.id);
                deviceData.bitWidth = dAttr.config.bit_width;
                deviceData.sampleRate = dAttr.config.sample_rate;
                deviceData.numChannel = dAttr.config.ch_info.channels;
                deviceData.rotation_type = PAL_SPEAKER_ROTATION_LR;
                deviceData.ch_info = nullptr;
                if ((PAL_DEVICE_OUT_SPEAKER == dAttr.id) &&
                    (2 == dAttr.config.ch_info.channels)) {
                    // Stereo Speakers. Check for the rotation type
                    if (PAL_SPEAKER_ROTATION_RL ==
                                                rm->getCurrentRotationType()) {
                        // Rotation is of RL, so need to swap the channels
                        deviceData.rotation_type = PAL_SPEAKER_ROTATION_RL;
                    }
                }
                builder->payloadMFCConfig((uint8_t**)&payload, &payloadSize, miid, &deviceData);
                if (payloadSize) {
                    status = updateCustomPayload(payload, payloadSize);
                    delete payload;
                    if(0 != status) {
                        PAL_ERR(LOG_TAG,"updateCustomPayload Failed\n");
                        return status;
                    }
                }
                if (isGaplessFmt) {
                    status = configureEarlyEOSDelay();
                }

                status = SessionAlsaUtils::setMixerParameter(mixer, compressDevIds.at(0),
                                                             customPayload, customPayloadSize);
                if (customPayload) {
                    free(customPayload);
                    customPayload = NULL;
                    customPayloadSize = 0;
                }

                if (status != 0) {
                    PAL_ERR(LOG_TAG,"setMixerParameter failed");
                    return status;
                }
            }
            break;
        default:
            break;
    }

    // Setting the volume as no default volume is set now in stream open
    if (setConfig(s, CALIBRATION, TAG_STREAM_VOLUME) != 0) {
            PAL_ERR(LOG_TAG,"Setting volume failed");
    }

free_feIds:
   return status;
}

int SessionAlsaCompress::pause(Stream * s __unused)
{
    int32_t status = 0;

    if (compress && playback_started) {
        status = compress_pause(compress);
        if (status == 0)
            playback_paused = true;
    }
   return status;
}

int SessionAlsaCompress::resume(Stream * s __unused)
{
    int32_t status = 0;

    if (compress && playback_paused) {
        status = compress_resume(compress);
        if (status == 0)
            playback_paused = false;
    }
   return status;
}

int SessionAlsaCompress::stop(Stream * s __unused)
{
    int32_t status = 0;

    if (compress && playback_started)
        status = compress_stop(compress);
   return status;
}

int SessionAlsaCompress::close(Stream * s)
{
    struct pal_stream_attributes sAttr;
    std::ostringstream disconnectCtrlName;
    s->getStreamAttributes(&sAttr);
    if (!compress) {
        if (compressDevIds.size() != 0)
            rm->freeFrontEndIds(compressDevIds, sAttr, 0);
        if (rm->cardState == CARD_STATUS_OFFLINE) {
            if (sessionCb)
                sessionCb(cbCookie, PAL_STREAM_CBK_EVENT_ERROR, NULL, 0);
        }
        return -EINVAL;
    }
    disconnectCtrlName << "COMPRESS" << compressDevIds.at(0) << " disconnect";
    disconnectCtrl = mixer_get_ctl_by_name(mixer, disconnectCtrlName.str().data());
    if (!disconnectCtrl) {
        PAL_ERR(LOG_TAG, "invalid mixer control: %s", disconnectCtrlName.str().data());
        return -EINVAL;
    }
    /** Disconnect FE to BE */
    mixer_ctl_set_enum_by_string(disconnectCtrl, rxAifBackEnds[0].second.data());
    compress_close(compress);
    PAL_DBG(LOG_TAG, "out of compress close");

    rm->freeFrontEndIds(compressDevIds, sAttr, 0);
    if (customPayload) {
        free(customPayload);
        customPayload = NULL;
        customPayloadSize = 0;
    }

    if (rm->cardState == CARD_STATUS_OFFLINE) {
        std::shared_ptr<offload_msg> msg = std::make_shared<offload_msg>(OFFLOAD_CMD_ERROR);
        std::lock_guard<std::mutex> lock(cv_mutex_);
        msg_queue_.push(msg);
        cv_.notify_all();
    }
    {
        std::shared_ptr<offload_msg> msg = std::make_shared<offload_msg>(OFFLOAD_CMD_EXIT);
        std::lock_guard<std::mutex> lock(cv_mutex_);
        msg_queue_.push(msg);
        cv_.notify_all();
    }

    /* wait for handler to exit */
    worker_thread->join();
    worker_thread.reset(NULL);

    /* empty the pending messages in queue */
    while (!msg_queue_.empty())
        msg_queue_.pop();

    return 0;
}

int SessionAlsaCompress::read(Stream *s __unused, int tag __unused, struct pal_buffer *buf __unused, int * size __unused)
{
    return 0;
}

int SessionAlsaCompress::fileWrite(Stream *s __unused, int tag __unused, struct pal_buffer *buf, int * size, int flag __unused)
{
    std::fstream fs;
    PAL_DBG(LOG_TAG, "Enter.");

    fs.open ("/data/testcompr.wav", std::fstream::binary | std::fstream::out | std::fstream::app);
    PAL_ERR(LOG_TAG, "file open success");
    char * buff=static_cast<char *>(buf->buffer);
    fs.write (buff,buf->size);
    PAL_ERR(LOG_TAG, "file write success");
    fs.close();
    PAL_ERR(LOG_TAG, "file close success");
    *size = (int)(buf->size);
    PAL_ERR(LOG_TAG,"iExit. size: %d", *size);
    return 0;
}

int SessionAlsaCompress::write(Stream *s __unused, int tag __unused, struct pal_buffer *buf, int * size, int flag __unused)
{
    int bytes_written = 0;
    int status;
    bool non_blocking = (!!ioMode);
    if (!buf || !(buf->buffer) || !(buf->size)) {
        PAL_VERBOSE(LOG_TAG, "buf: %pK, size: %zu",
                    buf, (buf ? buf->size : 0));
        return -EINVAL;
    }
    if (!compress) {
        PAL_ERR(LOG_TAG, "NULL pointer access,compress is invalid");
        return -EINVAL;
    }
    PAL_DBG(LOG_TAG, "buf->size is %zu buf->buffer is %pK ",
            buf->size, buf->buffer);

    bytes_written = compress_write(compress, buf->buffer, buf->size);

    PAL_VERBOSE(LOG_TAG, "writing buffer (%zu bytes) to compress device returned %d",
             buf->size, bytes_written);

    if (bytes_written >= 0 && bytes_written < (ssize_t)buf->size && non_blocking) {
        PAL_DBG(LOG_TAG, "No space available in compress driver, post msg to cb thread");
        std::shared_ptr<offload_msg> msg = std::make_shared<offload_msg>(OFFLOAD_CMD_WAIT_FOR_BUFFER);
        std::lock_guard<std::mutex> lock(cv_mutex_);
        msg_queue_.push(msg);

        cv_.notify_all();
    }

    if (!playback_started && bytes_written > 0) {
        status = compress_start(compress);
        if (status) {
            PAL_ERR(LOG_TAG, "compress start failed with err %d", status);
            return status;
        }
        playback_started = true;
    }

    if (size)
        *size = bytes_written;
    return 0;
}

int SessionAlsaCompress::readBufferInit(Stream *s __unused, size_t noOfBuf __unused, size_t bufSize __unused, int flag __unused)
{
    return 0;
}
int SessionAlsaCompress::writeBufferInit(Stream *s __unused, size_t noOfBuf __unused, size_t bufSize __unused, int flag __unused)
{
    return 0;
}

struct mixer_ctl* SessionAlsaCompress::getFEMixerCtl(const char *controlName, int *device)
{
    *device = compressDevIds.at(0);
    std::ostringstream CntrlName;
    struct mixer_ctl *ctl;

    CntrlName << "COMPRESS" << compressDevIds.at(0) << " " << controlName;
    ctl = mixer_get_ctl_by_name(mixer, CntrlName.str().data());
    if (!ctl) {
        PAL_ERR(LOG_TAG, "Invalid mixer control: %s\n", CntrlName.str().data());
        return nullptr;
    }

    return ctl;
}

uint32_t SessionAlsaCompress::getMIID(const char *backendName, uint32_t tagId, uint32_t *miid)
{
    int status = 0;
    int device = compressDevIds.at(0);

    status = SessionAlsaUtils::getModuleInstanceId(mixer, device,
                                                   backendName,
                                                   tagId, miid);
    if (0 != status)
        PAL_ERR(LOG_TAG, "Failed to get tag info %x, status = %d", tagId, status);

    return status;
}

int SessionAlsaCompress::setParameters(Stream *s __unused, int tagId, uint32_t param_id, void *payload)
{
    pal_param_payload *param_payload = (pal_param_payload *)payload;
    int status = 0;
    int device = compressDevIds.at(0);
    uint8_t* alsaParamData = NULL;
    size_t alsaPayloadSize = 0;
    uint32_t miid = 0;
    pal_snd_dec_t *pal_snd_dec = nullptr;
    effect_pal_payload_t *effectPalPayload = nullptr;
    struct compr_gapless_mdata mdata;
    struct pal_compr_gapless_mdata *gaplessMdata = NULL;

    switch (param_id) {
        case PAL_PARAM_ID_DEVICE_ROTATION:
        {
            pal_param_device_rotation_t *rotation =
                                     (pal_param_device_rotation_t *)payload;
            status = handleDeviceRotation(s, rotation->rotation_type, device, mixer,
                                          builder, rxAifBackEnds);
        }
        break;
        case PAL_PARAM_ID_UIEFFECT:
        {
            pal_effect_custom_payload_t *customPayload;
            param_payload = (pal_param_payload *)payload;
            effectPalPayload = (effect_pal_payload_t *)(param_payload->payload);
            status = SessionAlsaUtils::getModuleInstanceId(mixer, device,
                                                           rxAifBackEnds[0].second.data(),
                                                           tagId, &miid);
            if (0 != status) {
                PAL_ERR(LOG_TAG, "Failed to get tag info %x, status = %d", tagId, status);
                break;
            } else {
                customPayload = (pal_effect_custom_payload_t *)effectPalPayload->payload;
                status = builder->payloadCustomParam(&alsaParamData, &alsaPayloadSize,
                            customPayload->data,
                            effectPalPayload->payloadSize - sizeof(uint32_t),
                            miid, customPayload->paramId);
                if (status != 0) {
                    PAL_ERR(LOG_TAG, "payloadCustomParam failed. status = %d",
                                status);
                    break;
                }
                status = SessionAlsaUtils::setMixerParameter(mixer,
                                                             compressDevIds.at(0),
                                                             alsaParamData,
                                                             alsaPayloadSize);
                PAL_INFO(LOG_TAG, "mixer set param status=%d\n", status);
                free(alsaParamData);
            }
            break;
        }
        case PAL_PARAM_ID_BT_A2DP_TWS_CONFIG:
        {
            pal_bt_tws_payload *tws_payload = (pal_bt_tws_payload *)payload;
            status = SessionAlsaUtils::getModuleInstanceId(mixer, device,
                               rxAifBackEnds[0].second.data(), tagId, &miid);
            if (0 != status) {
                PAL_ERR(LOG_TAG, "Failed to get tag info %x, status = %d", tagId, status);
                return status;
            }

            builder->payloadTWSConfig(&alsaParamData, &alsaPayloadSize,
                 miid, tws_payload->isTwsMonoModeOn, tws_payload->codecFormat);
            if (alsaPayloadSize) {
                status = SessionAlsaUtils::setMixerParameter(mixer, device,
                                               alsaParamData, alsaPayloadSize);
                PAL_INFO(LOG_TAG, "mixer set tws config status=%d\n", status);
                free(alsaParamData);
            }
            return 0;
        }
        case PAL_PARAM_ID_CODEC_CONFIGURATION:
            pal_snd_dec = (pal_snd_dec_t *)param_payload->payload;
            PAL_DBG(LOG_TAG, "compress format %x", audio_fmt);
            switch (audio_fmt) {
                case PAL_AUDIO_FMT_MP3:
                case PAL_AUDIO_FMT_COMPRESSED_EXTENDED_RANGE_END:
                      break;
                case PAL_AUDIO_FMT_DEFAULT_PCM:
                     break;
#ifdef SND_COMPRESS_DEC_HDR
                case PAL_AUDIO_FMT_COMPRESSED_RANGE_BEGIN:
                case PAL_AUDIO_FMT_COMPRESSED_EXTENDED_RANGE_BEGIN:
                    break;
                case PAL_AUDIO_FMT_AAC:
                    codec.format = SND_AUDIOSTREAMFORMAT_RAW;
                    codec.options.aac_dec.audio_obj_type =
                                            pal_snd_dec->aac_dec.audio_obj_type;
                    codec.options.aac_dec.pce_bits_size =
                                            pal_snd_dec->aac_dec.pce_bits_size;
                    PAL_VERBOSE(LOG_TAG, "format- %x audio_obj_type- %x pce_bits_size- %x",
                                codec.format, codec.options.aac_dec.audio_obj_type,
                                codec.options.aac_dec.pce_bits_size);
                    break;
                case PAL_AUDIO_FMT_AAC_ADTS:
                    codec.format = SND_AUDIOSTREAMFORMAT_MP4ADTS;
                    codec.options.aac_dec.audio_obj_type =
                                            pal_snd_dec->aac_dec.audio_obj_type;
                    codec.options.aac_dec.pce_bits_size =
                                            pal_snd_dec->aac_dec.pce_bits_size;
                    PAL_VERBOSE(LOG_TAG, "format- %x audio_obj_type- %x pce_bits_size- %x",
                                codec.format, codec.options.aac_dec.audio_obj_type,
                                codec.options.aac_dec.pce_bits_size);
                    break;
                case PAL_AUDIO_FMT_AAC_ADIF:
                    codec.format = SND_AUDIOSTREAMFORMAT_ADIF;
                    codec.options.aac_dec.audio_obj_type =
                                            pal_snd_dec->aac_dec.audio_obj_type;
                    codec.options.aac_dec.pce_bits_size =
                                            pal_snd_dec->aac_dec.pce_bits_size;
                    PAL_VERBOSE(LOG_TAG, "format- %x audio_obj_type- %x pce_bits_size- %x",
                                codec.format, codec.options.aac_dec.audio_obj_type,
                                codec.options.aac_dec.pce_bits_size);
                    break;
                case PAL_AUDIO_FMT_AAC_LATM:
                    codec.format = SND_AUDIOSTREAMFORMAT_MP4LATM;
                    codec.options.aac_dec.audio_obj_type =
                                             pal_snd_dec->aac_dec.audio_obj_type;
                    codec.options.aac_dec.pce_bits_size =
                                            pal_snd_dec->aac_dec.pce_bits_size;
                    PAL_VERBOSE(LOG_TAG, "format- %x audio_obj_type- %x pce_bits_size- %x",
                                codec.format, codec.options.aac_dec.audio_obj_type,
                                codec.options.aac_dec.pce_bits_size);
                    break;
                case PAL_AUDIO_FMT_WMA_STD:
                    codec.format = pal_snd_dec->wma_dec.fmt_tag;
                    codec.options.wma_dec.super_block_align =
                                        pal_snd_dec->wma_dec.super_block_align;
                    codec.options.wma_dec.bits_per_sample =
                                            pal_snd_dec->wma_dec.bits_per_sample;
                    codec.options.wma_dec.channelmask =
                                            pal_snd_dec->wma_dec.channelmask;
                    codec.options.wma_dec.encodeopt =
                                            pal_snd_dec->wma_dec.encodeopt;
                    codec.options.wma_dec.encodeopt1 =
                                            pal_snd_dec->wma_dec.encodeopt1;
                    codec.options.wma_dec.encodeopt2 =
                                            pal_snd_dec->wma_dec.encodeopt2;
                    codec.options.wma_dec.avg_bit_rate =
                                            pal_snd_dec->wma_dec.avg_bit_rate;
                    PAL_VERBOSE(LOG_TAG, "format- %x super_block_align- %x bits_per_sample- %x"
                                         ", channelmask - %x \n", codec.format,
                               codec.options.wma_dec.super_block_align,
                               codec.options.wma_dec.bits_per_sample,
                               codec.options.wma_dec.channelmask);
                    PAL_VERBOSE(LOG_TAG, "encodeopt - %x, encodeopt1 - %x, encodeopt2 - %x"
                                         ", avg_bit_rate - %x \n", codec.options.wma_dec.encodeopt,
                                codec.options.wma_dec.encodeopt1, codec.options.wma_dec.encodeopt2,
                                codec.options.wma_dec.avg_bit_rate);
                    break;
                case PAL_AUDIO_FMT_WMA_PRO:
                    codec.format = pal_snd_dec->wma_dec.fmt_tag;
                    codec.options.wma_dec.super_block_align =
                                         pal_snd_dec->wma_dec.super_block_align;
                    codec.options.wma_dec.bits_per_sample =
                                         pal_snd_dec->wma_dec.bits_per_sample;
                    codec.options.wma_dec.channelmask =
                                         pal_snd_dec->wma_dec.channelmask;
                    codec.options.wma_dec.encodeopt =
                                         pal_snd_dec->wma_dec.encodeopt;
                    codec.options.wma_dec.encodeopt1 =
                                         pal_snd_dec->wma_dec.encodeopt1;
                    codec.options.wma_dec.encodeopt2 =
                                         pal_snd_dec->wma_dec.encodeopt2;
                    codec.options.wma_dec.avg_bit_rate =
                                         pal_snd_dec->wma_dec.avg_bit_rate;
                    PAL_VERBOSE(LOG_TAG, "format- %x super_block_align- %x"
                                         "bits_per_sample- %x channelmask- %x\n",
                                codec.format, codec.options.wma_dec.super_block_align,
                                codec.options.wma_dec.bits_per_sample,
                                codec.options.wma_dec.channelmask);
                    PAL_VERBOSE(LOG_TAG, "encodeopt- %x encodeopt1- %x"
                                         "encodeopt2- %x avg_bit_rate- %x\n",
                                codec.options.wma_dec.encodeopt,
                                codec.options.wma_dec.encodeopt1,
                                codec.options.wma_dec.encodeopt2,
                                codec.options.wma_dec.avg_bit_rate);
                    break;
                case PAL_AUDIO_FMT_ALAC:
                    codec.options.alac_dec.frame_length =
                                           pal_snd_dec->alac_dec.frame_length;
                    codec.options.alac_dec.compatible_version =
                                           pal_snd_dec->alac_dec.compatible_version;
                    codec.options.alac_dec.bit_depth =
                                           pal_snd_dec->alac_dec.bit_depth;
                    codec.options.alac_dec.pb =
                                           pal_snd_dec->alac_dec.pb;
                    codec.options.alac_dec.mb =
                                           pal_snd_dec->alac_dec.mb;
                    codec.options.alac_dec.kb =
                                           pal_snd_dec->alac_dec.kb;
                    codec.options.alac_dec.num_channels =
                                           pal_snd_dec->alac_dec.num_channels;
                    codec.options.alac_dec.max_run =
                                           pal_snd_dec->alac_dec.max_run;
                    codec.options.alac_dec.max_frame_bytes =
                                           pal_snd_dec->alac_dec.max_frame_bytes;
                    codec.options.alac_dec.avg_bit_rate =
                                           pal_snd_dec->alac_dec.avg_bit_rate;
                    codec.options.alac_dec.sample_rate =
                                           pal_snd_dec->alac_dec.sample_rate;
                    codec.options.alac_dec.channel_layout_tag =
                                           pal_snd_dec->alac_dec.channel_layout_tag;
                    PAL_VERBOSE(LOG_TAG, "frame_length- %x compatible_version- %x"
                                         "bit_depth- %x pb- %x mb- %x kb- %x",
                                codec.options.alac_dec.frame_length,
                                codec.options.alac_dec.compatible_version,
                                codec.options.alac_dec.bit_depth,
                                codec.options.alac_dec.pb, codec.options.alac_dec.mb,
                                codec.options.alac_dec.kb);
                    PAL_VERBOSE(LOG_TAG, "num_channels- %x max_run- %x"
                                         "max_frame_bytes- %x avg_bit_rate- %x"
                                         "sample_rate- %x channel_layout_tag- %x",
                                codec.options.alac_dec.num_channels,
                                codec.options.alac_dec.max_run,
                                codec.options.alac_dec.max_frame_bytes,
                                codec.options.alac_dec.avg_bit_rate,
                                codec.options.alac_dec.sample_rate,
                                codec.options.alac_dec.channel_layout_tag);
                    break;
                case PAL_AUDIO_FMT_APE:
                    codec.options.ape_dec.compatible_version =
                                       pal_snd_dec->ape_dec.compatible_version;
                    codec.options.ape_dec.compression_level =
                                       pal_snd_dec->ape_dec.compression_level;
                    codec.options.ape_dec.format_flags =
                                       pal_snd_dec->ape_dec.format_flags;
                    codec.options.ape_dec.blocks_per_frame =
                                       pal_snd_dec->ape_dec.blocks_per_frame;
                    codec.options.ape_dec.final_frame_blocks =
                                       pal_snd_dec->ape_dec.final_frame_blocks;
                    codec.options.ape_dec.total_frames =
                                       pal_snd_dec->ape_dec.total_frames;
                    codec.options.ape_dec.bits_per_sample =
                                       pal_snd_dec->ape_dec.bits_per_sample;
                    codec.options.ape_dec.num_channels =
                                       pal_snd_dec->ape_dec.num_channels;
                    codec.options.ape_dec.sample_rate =
                                       pal_snd_dec->ape_dec.sample_rate;
                    codec.options.ape_dec.seek_table_present =
                                       pal_snd_dec->ape_dec.seek_table_present;
                    PAL_VERBOSE(LOG_TAG, "compatible_version- %x compression_level- %x "
                                         "format_flags- %x blocks_per_frame- %x "
                                         "final_frame_blocks - %x",
                                codec.options.ape_dec.compatible_version,
                                codec.options.ape_dec.compression_level,
                                codec.options.ape_dec.format_flags,
                                codec.options.ape_dec.blocks_per_frame,
                                codec.options.ape_dec.final_frame_blocks);
                    PAL_VERBOSE(LOG_TAG, "total_frames- %x bits_per_sample- %x"
                                         " num_channels- %x sample_rate- %x"
                                         " seek_table_present - %x",
                                codec.options.ape_dec.total_frames,
                                codec.options.ape_dec.bits_per_sample,
                                codec.options.ape_dec.num_channels,
                                codec.options.ape_dec.sample_rate,
                                codec.options.ape_dec.seek_table_present);
                    break;
                case PAL_AUDIO_FMT_FLAC:
                    codec.format = SND_AUDIOSTREAMFORMAT_FLAC;
                    codec.options.flac_dec.sample_size =
                                       pal_snd_dec->flac_dec.sample_size;
                    codec.options.flac_dec.min_blk_size =
                                       pal_snd_dec->flac_dec.min_blk_size;
                    codec.options.flac_dec.max_blk_size =
                                       pal_snd_dec->flac_dec.max_blk_size;
                    codec.options.flac_dec.min_frame_size =
                                       pal_snd_dec->flac_dec.min_frame_size;
                    codec.options.flac_dec.max_frame_size =
                                       pal_snd_dec->flac_dec.max_frame_size;
                    PAL_VERBOSE(LOG_TAG, "sample_size- %x min_blk_size- %x "
                                         "max_blk_size- %x min_frame_size- %x "
                                         "max_frame_size- %x",
                                codec.options.flac_dec.sample_size,
                                codec.options.flac_dec.min_blk_size,
                                codec.options.flac_dec.max_blk_size,
                                codec.options.flac_dec.min_frame_size,
                                codec.options.flac_dec.max_frame_size);
                    break;
                case PAL_AUDIO_FMT_FLAC_OGG:
                    codec.format = SND_AUDIOSTREAMFORMAT_FLAC_OGG;
                    codec.options.flac_dec.sample_size =
                                      pal_snd_dec->flac_dec.sample_size;
                    codec.options.flac_dec.min_blk_size =
                                      pal_snd_dec->flac_dec.min_blk_size;
                    codec.options.flac_dec.max_blk_size =
                                      pal_snd_dec->flac_dec.max_blk_size;
                    codec.options.flac_dec.min_frame_size =
                                      pal_snd_dec->flac_dec.min_frame_size;
                    codec.options.flac_dec.max_frame_size =
                                      pal_snd_dec->flac_dec.max_frame_size;
                    PAL_VERBOSE(LOG_TAG, "sample_size- %x min_blk_size- %x "
                                         "max_blk_size- %x min_frame_size- %x "
                                         "max_frame_size - %x",
                                codec.options.flac_dec.sample_size,
                                codec.options.flac_dec.min_blk_size,
                                codec.options.flac_dec.max_blk_size,
                                codec.options.flac_dec.min_frame_size,
                                codec.options.flac_dec.max_frame_size);
                    break;
#endif
                case PAL_AUDIO_FMT_VORBIS:
                    codec.format = pal_snd_dec->vorbis_dec.bit_stream_fmt;
                    break;
                default:
                    PAL_ERR(LOG_TAG, "Entered default, format %x", audio_fmt);
                    break;
            }
        break;
    case PAL_PARAM_ID_GAPLESS_MDATA:
         if (!compress) {
             PAL_ERR(LOG_TAG, "Compress is invalid");
             return -EINVAL;
         }
         if (isGaplessFmt) {
             gaplessMdata = (pal_compr_gapless_mdata*)param_payload->payload;
             PAL_DBG(LOG_TAG, "Setting gapless metadata %d %d",
                               gaplessMdata->encoderDelay, gaplessMdata->encoderPadding);
             mdata.encoder_delay = gaplessMdata->encoderDelay;
             mdata.encoder_padding = gaplessMdata->encoderPadding;
             status = compress_set_gapless_metadata(compress, &mdata);
             if (status != 0) {
                 PAL_ERR(LOG_TAG, "set gapless metadata failed");
                 return status;
             }
             } else {
                 PAL_ERR(LOG_TAG, "audio fmt %x is not gapless", audio_fmt);
                 return -EINVAL;
         }
         break;
    default:
        PAL_INFO(LOG_TAG, "Unsupported param id %u", param_id);
        break;
    }

    return 0;
}

int SessionAlsaCompress::registerCallBack(session_callback cb, void *cookie)
{
    sessionCb = cb;
    cbCookie = cookie;
    return 0;
}

int SessionAlsaCompress::flush()
{
    int status = 0;

    if (!compress) {
        PAL_ERR(LOG_TAG, "Compress is invalid");
        return -EINVAL;
    }
    if (playback_started) {
        PAL_VERBOSE(LOG_TAG,"Enter flush\n");
        status = compress_stop(compress);
        if (!status) {
            playback_started = false;
        }
    }
    PAL_VERBOSE(LOG_TAG,"playback_started %d status %d\n", playback_started,
            status);
    return status;
}

int SessionAlsaCompress::drain(pal_drain_type_t type)
{
    std::shared_ptr<offload_msg> msg;

    if (!compress) {
       PAL_ERR(LOG_TAG, "compress is invalid");
       return -EINVAL;
    }

    PAL_VERBOSE(LOG_TAG, "drain type = %d", type);

    switch (type) {
    case PAL_DRAIN:
    {
        msg = std::make_shared<offload_msg>(OFFLOAD_CMD_DRAIN);
        std::lock_guard<std::mutex> drain_lock(cv_mutex_);
        msg_queue_.push(msg);
        cv_.notify_all();
    }
    break;

    case PAL_DRAIN_PARTIAL:
    {
        msg = std::make_shared<offload_msg>(OFFLOAD_CMD_PARTIAL_DRAIN);
        std::lock_guard<std::mutex> partial_lock(cv_mutex_);
        msg_queue_.push(msg);
        cv_.notify_all();
    }
    break;

    default:
        PAL_ERR(LOG_TAG, "invalid drain type = %d", type);
        return -EINVAL;
    }

    return 0;
}

int SessionAlsaCompress::getParameters(Stream *s __unused, int tagId __unused, uint32_t param_id __unused, void **payload __unused)
{
    return 0;
}

int SessionAlsaCompress::getTimestamp(struct pal_session_time *stime)
{
    int status = 0;
    status = SessionAlsaUtils::getTimestamp(mixer, compressDevIds, spr_miid, stime);
    if (0 != status) {
       PAL_ERR(LOG_TAG, "getTimestamp failed status = %d", status);
       return status;
    }
    return status;
}

int SessionAlsaCompress::setECRef(Stream *s __unused, std::shared_ptr<Device> rx_dev __unused, bool is_enable __unused)
{
    return 0;
}

