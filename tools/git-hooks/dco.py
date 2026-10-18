"""
@file  dco.py
@brief 提交信息中签署与署名的识别规则，供 commit-msg 与 pre-push 共用

- DCO（CONTRIBUTING.md §3.7）：只能由人类签署，AI 不得以任何名义添加
  Signed-off-by。能识别两种违规：签署者的姓名或邮箱像 AI；在 AI 助手的
  会话里提交时带有签署。AI 冒用人类姓名签署识别不了，靠规则和评审约束。
- 署名（§3.8）：不接受 AI 署名为共同作者，也不接受"Generated with
  Claude Code"之类的署名行。
"""

import os
import re

SIGNOFF_RE = re.compile(r"^Signed-off-by:\s*(.*)$")
CO_AUTHOR_RE = re.compile(r"^Co-Authored-By:\s*(.*)$", re.IGNORECASE)
# 整行就是一条署名才算：可带 🤖 前缀，可以是 Markdown 链接；行内提到不算
GENERATED_BY_RE = re.compile(
    r"^\s*(\U0001F916\s*)?generated (with|by) \[?claude[\w .-]*\]?(\([^)]*\))?\s*$",
    re.IGNORECASE)
VALID_SIGNOFF_RE = re.compile(r"^[^<>]+ <[^<>@\s]+@[^<>\s]+>$")
# 常见 AI 助手的名字与厂商；只用于拦截，不代表允许名单之外的 AI 签署
AI_IDENTITY_RE = re.compile(
    r"claude|anthropic|copilot|openai|chatgpt|\bgpt\b|codex|gemini",
    re.IGNORECASE)
# AI 助手在其运行的命令中设置的环境变量
AGENT_ENV_VARS = ("CLAUDECODE",)


def signoffs(lines):
    """返回提交信息里所有 Signed-off-by 的值。"""
    return [m.group(1).strip() for l in lines if (m := SIGNOFF_RE.match(l))]


def is_ai_identity(value):
    return bool(AI_IDENTITY_RE.search(value))


def ai_attributions(lines):
    """返回提交信息里的 AI 署名行：AI 的 Co-Authored-By，或 Generated with 行。"""
    out = []
    for l in lines:
        m = CO_AUTHOR_RE.match(l)
        if (m and is_ai_identity(m.group(1))) or GENERATED_BY_RE.match(l):
            out.append(l.strip())
    return out


def human_signoffs(lines):
    return [v for v in signoffs(lines) if not is_ai_identity(v)]


def in_agent_session():
    """当前进程是否运行在 AI 助手的会话里。"""
    return any(os.environ.get(v) for v in AGENT_ENV_VARS)
