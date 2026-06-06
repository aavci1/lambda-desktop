#pragma once

/// \file Lambda/UI/KeyCodes.hpp
///
/// Part of the Lambda public API.


#include <cstdint>

#include <Lambda/UI/Input.hpp>

namespace lambda::keys {

constexpr std::uint16_t Unknown = 0xFFFF;

// Printable keys (macOS virtual key codes, layout-independent)
constexpr std::uint16_t A = 0x00;
constexpr std::uint16_t S = 0x01;
constexpr std::uint16_t D = 0x02;
constexpr std::uint16_t F = 0x03;
constexpr std::uint16_t H = 0x04;
constexpr std::uint16_t G = 0x05;
constexpr std::uint16_t Z = 0x06;
constexpr std::uint16_t X = 0x07;
constexpr std::uint16_t C = 0x08;
constexpr std::uint16_t V = 0x09;
constexpr std::uint16_t B = 0x0B;
constexpr std::uint16_t Q = 0x0C;
constexpr std::uint16_t W = 0x0D;
constexpr std::uint16_t E = 0x0E;
constexpr std::uint16_t R = 0x0F;
constexpr std::uint16_t Y = 0x10;
constexpr std::uint16_t T = 0x11;
constexpr std::uint16_t Digit1 = 0x12;
constexpr std::uint16_t Digit2 = 0x13;
constexpr std::uint16_t Digit3 = 0x14;
constexpr std::uint16_t Digit4 = 0x15;
constexpr std::uint16_t Digit6 = 0x16;
constexpr std::uint16_t Digit5 = 0x17;
constexpr std::uint16_t Equal = 0x18;
constexpr std::uint16_t Digit9 = 0x19;
constexpr std::uint16_t Digit7 = 0x1A;
constexpr std::uint16_t Minus = 0x1B;
constexpr std::uint16_t Digit8 = 0x1C;
constexpr std::uint16_t Digit0 = 0x1D;
constexpr std::uint16_t RightBracket = 0x1E;
constexpr std::uint16_t N = 0x2D;
constexpr std::uint16_t O = 0x1F;
constexpr std::uint16_t U = 0x20;
constexpr std::uint16_t LeftBracket = 0x21;
constexpr std::uint16_t I = 0x22;
constexpr std::uint16_t P = 0x23;
constexpr std::uint16_t L = 0x25;
constexpr std::uint16_t J = 0x26;
constexpr std::uint16_t Quote = 0x27;
constexpr std::uint16_t K = 0x28;
constexpr std::uint16_t Semicolon = 0x29;
constexpr std::uint16_t Backslash = 0x2A;
constexpr std::uint16_t Comma = 0x2B;
constexpr std::uint16_t Slash = 0x2C;
constexpr std::uint16_t M = 0x2E;
constexpr std::uint16_t Period = 0x2F;
constexpr std::uint16_t Grave = 0x32;

// Editing keys
constexpr std::uint16_t Return = 0x24;
constexpr std::uint16_t Tab = 0x30;
constexpr std::uint16_t Space = 0x31;
constexpr std::uint16_t Delete = 0x33;        // Backspace (labelled Delete on Mac keyboards)
constexpr std::uint16_t ForwardDelete = 0x75; // Fn+Delete
constexpr std::uint16_t Escape = 0x35;

// Navigation keys
constexpr std::uint16_t LeftArrow = 0x7B;
constexpr std::uint16_t RightArrow = 0x7C;
constexpr std::uint16_t DownArrow = 0x7D;
constexpr std::uint16_t UpArrow = 0x7E;
constexpr std::uint16_t Home = 0x73;
constexpr std::uint16_t End = 0x77;
constexpr std::uint16_t PageUp = 0x74;
constexpr std::uint16_t PageDown = 0x79;

// Function keys
constexpr std::uint16_t F1 = 0x7A;
constexpr std::uint16_t F2 = 0x78;
constexpr std::uint16_t F3 = 0x63;
constexpr std::uint16_t F4 = 0x76;
constexpr std::uint16_t F5 = 0x60;
constexpr std::uint16_t F6 = 0x61;
constexpr std::uint16_t F7 = 0x62;
constexpr std::uint16_t F8 = 0x64;
constexpr std::uint16_t F9 = 0x65;
constexpr std::uint16_t F10 = 0x6D;
constexpr std::uint16_t F11 = 0x67;
constexpr std::uint16_t F12 = 0x6F;

} // namespace lambda::keys
