#ifndef RL_FEEDBACK_H
#define RL_FEEDBACK_H

#ifdef __cplusplus
extern "C" {
#endif

// The dynamic threshold multiplier that the agent adapts
extern float current_policy_threshold_multiplier;

/**
 * @brief Applies Reinforcement Learning feedback to adjust the threshold policy.
 * 
 * @param mae The current Mean Absolute Error from the inference.
 * @param current_temp The current physical temperature reading.
 * @param agent_action_anomaly True if the TinyML agent flagged an anomaly, False otherwise.
 */
void apply_rl_feedback(float mae, float current_temp, bool agent_action_anomaly);

#ifdef __cplusplus
}
#endif

#endif // RL_FEEDBACK_H
