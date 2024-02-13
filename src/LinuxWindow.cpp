#include <Graphics/Window.h>
#include <Graphics/LinuxWindow.h>

namespace aer::gfx
{
    ref_ptr<Window> Window::create( const WindowProperties& props )
    {
        return ref_ptr<Win32::Win32Window>( new Win32::Win32Window( { props } ) );
    }

    template<>
    HWND Window::native()
    {
        return static_cast<Win32::Win32Window*>( this )->native();
    };

} // namespace aer::gfx

namespace are::linux
{
    

    
} // namespace are::linux

