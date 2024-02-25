#include <Graphics/XCBWindow.h>

namespace aer::gfx
{
    ref_ptr<Window> Window::create( const WindowProperties& props )
    {
        return ref_ptr<linux::XCBWindow>( new linux::XCBWindow( { props } ) );
    }

    template<>
    linux::Window_t Window::native()
    {
        return static_cast<linux::XCBWindow*>( this )->native();
    };

} // namespace aer::gfx

namespace aer::linux
{
    
XCBWindow::XCBWindow( const WindowProperties& props )
    : _properties( props )
{
    
}
    
bool XCBWindow::PollEvents( Events& events, bool clear_unhandled )
{
    return Window::PollEvents( events, clear_unhandled );
}

} // namespace aer::linux