# BUGS

# GENERAL

- [ ] When a window is closed the chrome disappears first window contents disappear later. It should fade out as a whole.
- [ ] Minimized apps should go to the dock, with a preview. User should be able to restore them.
- [ ] Resizing the terminal app causes the content to be stretched, even if it is minimal. The framebuffer should be drawn without any scaling, just align to the top left unscaled.
- [ ] If an image is clicked in the finder. The image opens in Firefox, probably due to the MIME/type assignment. Ideally it should open in the Preview app. The other problem is that as long as the Firefox window is open in this case the files app can't be moved, closed etc. It doesn't receive the events.
