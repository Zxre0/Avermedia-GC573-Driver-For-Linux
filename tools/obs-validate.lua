obs = obslua
function finish_validation()
    obs.timer_remove(finish_validation)
    obs.obs_frontend_take_screenshot()
    if obs.obs_frontend_recording_active() then
        obs.obs_frontend_recording_stop()
    end
    obs.script_log(obs.LOG_INFO, 'GC573 validation recording completed; preview remains active.')
end
function script_load(settings)
    obs.timer_add(finish_validation, 15000)
end
function script_description()
    return 'One-shot GC573 driver validation: stop the test recording after 15 seconds.'
end
