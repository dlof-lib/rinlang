#pragma once
// rin_message_natives.h — نظام الرسائل الموحّد (docs/message.md)
// يُستدعى من Interpreter::registerNatives() عبر registerNativesMessage()

class Interpreter;
void registerNativesMessage(Interpreter& interp);
