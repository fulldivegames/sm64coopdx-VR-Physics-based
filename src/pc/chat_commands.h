#ifndef CHAT_COMMANDS_H
#define CHAT_COMMANDS_H

bool exec_chat_command(char* command);
void queue_chat_command(const char* command);
void exec_queued_chat_command(void);
void display_chat_commands(void);

#endif
