#include "rl_feedback.h"
#include "esp_log.h"
#include "app_config.h"

// Default policy starts at 3.0 standard deviations
float current_policy_threshold_multiplier = 3.0f;
static const float POLICY_LEARNING_RATE = 0.05f; // Lowered for stability
static const char* TAG = "RL_Agent";

static int updates_since_last_change = 0;
#define UPDATE_COOLDOWN 3 // Update policy at most every 3 reward cycles

void apply_rl_feedback(float mae, float current_temp, bool agent_action_anomaly) {
    float reward = 0.0f;
    const char* reason = "Unknown";
    
    // Environment Truth (Is the temperature actually dangerous?)
    bool is_actually_extreme = (current_temp > ENV_TEMP_MAX || current_temp < ENV_TEMP_MIN);

    // 1. Observe and Reward
    if (agent_action_anomaly && is_actually_extreme) {
        reward = 1.0f; 
        reason = "True Positive (Detected real danger)";
        ESP_LOGI(TAG, "[FEEDBACK] Reward: +1.0 | %s | Temp: %.2f", reason, current_temp);
    } 
    else if (agent_action_anomaly && !is_actually_extreme) {
        reward = -1.0f; 
        reason = "False Positive (False alarm)";
        ESP_LOGW(TAG, "[FEEDBACK] Reward: -1.0 | %s | Temp: %.2f", reason, current_temp);
    }
    else if (!agent_action_anomaly && is_actually_extreme) {
        reward = -2.0f; 
        reason = "False Negative (Missed danger)";
        ESP_LOGW(TAG, "[FEEDBACK] Reward: -2.0 | %s | Temp: %.2f", reason, current_temp);
    } 
    else {
        reward = 0.1f; 
        reason = "True Negative (Normal status)";
        ESP_LOGD(TAG, "[FEEDBACK] Reward: +0.1 | %s | Temp: %.2f", reason, current_temp);
    }

    // 2. Update Policy (Threshold Multiplier) based on Reward
    updates_since_last_change++;
    if (updates_since_last_change < UPDATE_COOLDOWN) return;

    float old_multiplier = current_policy_threshold_multiplier;
    if (reward == -1.0f) {
        // If false positive, the threshold is too tight, so we loosen it
        current_policy_threshold_multiplier += POLICY_LEARNING_RATE;
        updates_since_last_change = 0;
        ESP_LOGI(TAG, "[POLICY] Loosening: %.2f -> %.2f (Reason: %s)", old_multiplier, current_policy_threshold_multiplier, reason);
    } 
    else if (reward == -2.0f) {
        // If false negative, the threshold is too loose, we must tighten it
        current_policy_threshold_multiplier -= POLICY_LEARNING_RATE;
        if (current_policy_threshold_multiplier < 1.0f) {
            current_policy_threshold_multiplier = 1.0f; // Safety clamp
        }
        updates_since_last_change = 0;
        ESP_LOGI(TAG, "[POLICY] Tightening: %.2f -> %.2f (Reason: %s)", old_multiplier, current_policy_threshold_multiplier, reason);
    }
}
