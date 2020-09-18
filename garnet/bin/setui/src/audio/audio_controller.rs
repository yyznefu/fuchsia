// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::handler::base::{Event, SettingHandlerResult, State};
use crate::handler::setting_handler::persist::{
    controller as data_controller, write, ClientProxy, WriteResult,
};
use crate::handler::setting_handler::{controller, ControllerError};
use crate::input::{InputMonitor, InputMonitorHandle, InputType};
use async_trait::async_trait;
use {
    crate::audio::{
        create_default_modified_timestamps, default_audio_info, ModifiedTimestamps,
        StreamVolumeControl,
    },
    crate::internal::common::now,
    crate::switchboard::base::*,
    fuchsia_async as fasync,
    futures::lock::Mutex,
    std::collections::HashMap,
    std::sync::Arc,
};

fn get_streams_array_from_map(
    stream_map: &HashMap<AudioStreamType, StreamVolumeControl>,
) -> [AudioStream; 5] {
    let mut streams: [AudioStream; 5] = default_audio_info().streams;
    for i in 0..streams.len() {
        if let Some(volume_control) = stream_map.get(&streams[i].stream_type) {
            streams[i] = volume_control.stored_stream.clone();
        }
    }
    streams
}

type VolumeControllerHandle = Arc<Mutex<VolumeController>>;

pub struct VolumeController {
    client: ClientProxy<AudioInfo>,
    audio_service_connected: bool,
    stream_volume_controls: HashMap<AudioStreamType, StreamVolumeControl>,
    input_monitor: InputMonitorHandle<AudioInfo>,
    modified_timestamps: ModifiedTimestamps,
}

impl VolumeController {
    async fn create(client: ClientProxy<AudioInfo>) -> VolumeControllerHandle {
        let handle = Arc::new(Mutex::new(Self {
            client: client.clone(),
            stream_volume_controls: HashMap::new(),
            audio_service_connected: false,
            input_monitor: InputMonitor::create(
                client.clone(),
                vec![InputType::Microphone, InputType::VolumeButtons],
            ),
            modified_timestamps: create_default_modified_timestamps(),
        }));

        handle
    }

    /// Restores the necessary dependencies' state on boot.
    async fn restore(&mut self) -> ControllerStateResult {
        self.restore_volume_state(true).await
    }

    /// Extracts the audio state from persistent storage and restores it on
    /// the local state. Also pushes the changes to the audio core if
    /// [push_to_audio_core] is true.
    async fn restore_volume_state(&mut self, push_to_audio_core: bool) -> ControllerStateResult {
        let audio_info = self.client.read().await;
        let stored_streams = audio_info.streams.iter().cloned().collect();
        self.update_volume_streams(&stored_streams, push_to_audio_core).await?;
        Ok(())
    }

    async fn get_info(&mut self) -> Result<AudioInfo, ControllerError> {
        let mut audio_info = self.client.read().await;
        self.input_monitor.lock().await.ensure_monitor().await;

        audio_info.input =
            AudioInputInfo { mic_mute: self.input_monitor.lock().await.get_mute_state() };
        audio_info.modified_timestamps = Some(self.modified_timestamps.clone());
        Ok(audio_info)
    }

    async fn set_volume(&mut self, volume: Vec<AudioStream>) -> SettingHandlerResult {
        self.get_info().await?;

        // Update timestamps for changed streams.
        for stream in volume.iter() {
            self.modified_timestamps.insert(stream.stream_type, now().to_string());
        }

        if !(self.update_volume_streams(&volume, true).await?) {
            self.client.notify(Event::Changed).await;
        }

        Ok(None)
    }

    /// Updates the state with the given streams' volume levels.
    ///
    /// If [push_to_audio_core] is true, pushes the changes to the audio core.
    /// If not, just sets it on the local stored state. Should be called with
    /// true on first restore and on volume changes, and false otherwise.
    /// Returns whether the change triggered a notification.
    async fn update_volume_streams(
        &mut self,
        new_streams: &Vec<AudioStream>,
        push_to_audio_core: bool,
    ) -> Result<bool, ControllerError> {
        if push_to_audio_core {
            self.check_and_bind_volume_controls(&default_audio_info().streams.to_vec()).await?;
            for stream in new_streams {
                if let Some(volume_control) =
                    self.stream_volume_controls.get_mut(&stream.stream_type)
                {
                    volume_control.set_volume(stream.clone()).await?;
                }
            }
        } else {
            self.check_and_bind_volume_controls(new_streams).await?;
        }

        let mut stored_value = self.client.read().await;
        stored_value.streams = get_streams_array_from_map(&self.stream_volume_controls);

        Ok(write(&self.client, stored_value, false).await.notified())
    }

    /// Populates the local state with the given [streams] and binds it to the audio core service.
    async fn check_and_bind_volume_controls(
        &mut self,
        streams: &Vec<AudioStream>,
    ) -> ControllerStateResult {
        if self.audio_service_connected {
            return Ok(());
        }

        let service_result = self
            .client
            .get_service_context()
            .await
            .lock()
            .await
            .connect::<fidl_fuchsia_media::AudioCoreMarker>()
            .await;

        let audio_service = service_result.map_err(|_| {
            ControllerError::ExternalFailure(
                SettingType::Audio,
                "fuchsia.media.audio".into(),
                "connect for audio_core".into(),
            )
        })?;
        self.audio_service_connected = true;

        for stream in streams.iter() {
            let client = self.client.clone();
            let stream_volume_control = StreamVolumeControl::create(
                &audio_service,
                stream.clone(),
                Some(Arc::new(move || {
                    // When the StreamVolumeControl exits early, inform the
                    // proxy we have exited. The proxy will then cleanup this
                    // AudioController.
                    let client = client.clone();
                    fasync::Task::spawn(async move {
                        client
                            .notify(Event::Exited(Err(ControllerError::UnexpectedError(
                                "stream_volume_control exit".into(),
                            ))))
                            .await;
                    })
                    .detach();
                })),
                None,
            )
            .await?;
            self.stream_volume_controls.insert(stream.stream_type.clone(), stream_volume_control);
        }

        Ok(())
    }
}

pub struct AudioController {
    volume: VolumeControllerHandle,
}

#[async_trait]
impl data_controller::Create<AudioInfo> for AudioController {
    /// Creates the controller
    async fn create(client: ClientProxy<AudioInfo>) -> Result<Self, ControllerError> {
        Ok(AudioController { volume: VolumeController::create(client).await })
    }
}

#[async_trait]
impl controller::Handle for AudioController {
    async fn handle(&self, request: SettingRequest) -> Option<SettingHandlerResult> {
        #[allow(unreachable_patterns)]
        match request {
            SettingRequest::Restore => Some(self.volume.lock().await.restore().await.map(|_| None)),
            SettingRequest::SetVolume(volume) => {
                Some(self.volume.lock().await.set_volume(volume).await)
            }
            SettingRequest::Get => Some(match self.volume.lock().await.get_info().await {
                Ok(info) => Ok(Some(SettingResponse::Audio(info))),
                Err(e) => Err(e),
            }),
            _ => None,
        }
    }

    async fn change_state(&mut self, state: State) -> Option<ControllerStateResult> {
        match state {
            State::Startup => {
                // Restore the volume state locally but do not push to the audio core.
                Some(self.volume.lock().await.restore_volume_state(false).await)
            }
            _ => None,
        }
    }
}
